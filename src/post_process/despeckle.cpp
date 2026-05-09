#include "despeckle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "common/parallel_for.hpp"

// NOLINTBEGIN(readability-identifier-length,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-math-missing-parentheses,cppcoreguidelines-avoid-magic-numbers,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,modernize-use-auto,cppcoreguidelines-pro-bounds-constant-array-index,modernize-use-ranges,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
//
// despeckle tidy-suppression rationale.
//
// post-process pixel-math is OFX render hot path; per CLAUDE.md changes
// here are gated by the phase_8_gpu_prepare 10% regression budget, so we
// suppress diagnostics that would force restructuring without measurable
// safety value. operator[] sites index validated tensor strides
// (row_offset = y * width with y, x already clamped to image bounds), so
// .at() would add a redundant bounds check on every pixel. The
// (r, g, b, a, x, y, h, w) and (dx, dy, kx, ky) names are universal
// pixel-coord conventions; expanding them obscures the math. Magic
// numbers (255, 0.5F, OpenCV's 0.3 sigma formula) are canonical uint8
// quantization and Gaussian-kernel constants, and the union-find /
// connected-components orchestrators have linear flow whose helper
// extraction would just thread a dozen locals through new signatures.
namespace corridorkey {
namespace {

struct MaskRun {
    int y = 0;
    int x_begin = 0;
    int x_end = 0;
    int parent = 0;
    int rank = 0;
    int area = 0;
};

int find_run_root(std::vector<MaskRun>& runs, int index) {
    int root = index;
    while (runs[root].parent != root) {
        root = runs[root].parent;
    }
    while (runs[index].parent != index) {
        int next = runs[index].parent;
        runs[index].parent = root;
        index = next;
    }
    return root;
}

void unite_runs(std::vector<MaskRun>& runs, int a, int b) {
    a = find_run_root(runs, a);
    b = find_run_root(runs, b);
    if (a == b) return;
    if (runs[a].rank < runs[b].rank) {
        std::swap(a, b);
    }
    runs[b].parent = a;
    runs[a].area += runs[b].area;
    if (runs[a].rank == runs[b].rank) {
        ++runs[a].rank;
    }
}

void add_mask_run(std::vector<MaskRun>& runs, int y, int x_begin, int x_end, int previous_end,
                  int& previous_cursor) {
    const int index = static_cast<int>(runs.size());
    runs.push_back(MaskRun{
        .y = y,
        .x_begin = x_begin,
        .x_end = x_end,
        .parent = index,
        .rank = 0,
        .area = x_end - x_begin + 1,
    });

    while (previous_cursor < previous_end && runs[previous_cursor].x_end < x_begin - 1) {
        ++previous_cursor;
    }
    for (int candidate = previous_cursor;
         candidate < previous_end && runs[candidate].x_begin <= x_end + 1; ++candidate) {
        unite_runs(runs, index, candidate);
    }
}

void filter_components_rle(const std::vector<uint8_t>& mask, std::vector<uint8_t>& cleaned, int w,
                           int h, int area_threshold) {
    std::vector<MaskRun> runs;
    runs.reserve(static_cast<std::size_t>(h) * 8U);

    int previous_begin = 0;
    int previous_end = 0;

    for (int y = 0; y < h; ++y) {
        const int current_begin = static_cast<int>(runs.size());
        int previous_cursor = previous_begin;
        int x = 0;
        const int row_offset = y * w;
        while (x < w) {
            while (x < w && mask[row_offset + x] == 0) {
                ++x;
            }
            if (x >= w) {
                break;
            }
            const int x_begin = x;
            while (x + 1 < w && mask[row_offset + x + 1] != 0) {
                ++x;
            }
            add_mask_run(runs, y, x_begin, x, previous_end, previous_cursor);
            ++x;
        }
        previous_begin = current_begin;
        previous_end = static_cast<int>(runs.size());
    }

    std::fill(cleaned.begin(), cleaned.end(), 0);
    for (int i = 0; i < static_cast<int>(runs.size()); ++i) {
        const int root = find_run_root(runs, i);
        if (runs[root].area < area_threshold) {
            continue;
        }
        const std::size_t row_offset = static_cast<std::size_t>(runs[i].y) *
                                       static_cast<std::size_t>(w);
        std::fill(cleaned.begin() + static_cast<std::ptrdiff_t>(row_offset + runs[i].x_begin),
                  cleaned.begin() + static_cast<std::ptrdiff_t>(row_offset + runs[i].x_end + 1),
                  255);
    }
}

// Morphological dilation with elliptical kernel
void dilate_binary(std::vector<uint8_t>& mask, std::vector<uint8_t>& temp_result, int w, int h,
                   int radius) {
    if (radius <= 0) return;

    std::fill(temp_result.begin(), temp_result.end(), 0);
    std::vector<int> row_reach(static_cast<std::size_t>(2 * radius + 1), 0);
    const float center = static_cast<float>(radius);
    const float r_sq = (center + 0.5F) * (center + 0.5F);
    for (int dy = -radius; dy <= radius; ++dy) {
        const float remaining = r_sq - static_cast<float>(dy * dy);
        row_reach[static_cast<std::size_t>(dy + radius)] =
            remaining > 0.0F ? static_cast<int>(std::floor(std::sqrt(remaining))) : 0;
    }

    for (int y = 0; y < h; ++y) {
        const int row_offset = y * w;
        int x = 0;
        while (x < w) {
            while (x < w && mask[row_offset + x] == 0) {
                ++x;
            }
            if (x >= w) {
                break;
            }
            const int x_begin = x;
            while (x + 1 < w && mask[row_offset + x + 1] != 0) {
                ++x;
            }
            const int x_end = x;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= h) {
                    continue;
                }
                const int reach = row_reach[static_cast<std::size_t>(dy + radius)];
                const int fill_begin = std::max(0, x_begin - reach);
                const int fill_end = std::min(w - 1, x_end + reach);
                const auto begin = temp_result.begin() +
                                   static_cast<std::ptrdiff_t>(ny * w + fill_begin);
                const auto end = temp_result.begin() +
                                 static_cast<std::ptrdiff_t>(ny * w + fill_end + 1);
                std::fill(begin, end, 255);
            }
            ++x;
        }
    }

    mask.swap(temp_result);
}

struct MaskSummary {
    int foreground_count = 0;
};

MaskSummary threshold_mask(const Image& alpha, std::vector<uint8_t>& mask) {
    std::vector<int> row_counts(static_cast<std::size_t>(alpha.height), 0);
    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            int foreground_count = 0;
            size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(alpha.width);
            for (int x = 0; x < alpha.width; ++x) {
                size_t index = row_offset + static_cast<size_t>(x);
                const bool foreground = alpha.data[index] > 0.5F;
                mask[index] = foreground ? 255 : 0;
                foreground_count += foreground ? 1 : 0;
            }
            row_counts[static_cast<std::size_t>(y)] = foreground_count;
        }
    });
    return MaskSummary{
        .foreground_count = std::accumulate(row_counts.begin(), row_counts.end(), 0),
    };
}

void zero_alpha(Image alpha) {
    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(alpha.width);
            std::fill_n(alpha.data.data() + row_offset, static_cast<std::size_t>(alpha.width),
                        0.0F);
        }
    });
}

// 1D Gaussian kernel (OpenCV formula: sigma = 0.3*((ksize-1)*0.5 - 1) + 0.8 when sigma=0)
std::vector<float> make_gaussian_kernel(int half_size) {
    int ksize = 2 * half_size + 1;
    float sigma = 0.3F * (static_cast<float>(ksize - 1) * 0.5F - 1.0F) + 0.8F;
    std::vector<float> kernel(ksize);
    float sum = 0.0F;

    for (int i = 0; i < ksize; ++i) {
        float x = static_cast<float>(i - half_size);
        kernel[i] = std::exp(-0.5F * (x * x) / (sigma * sigma));
        sum += kernel[i];
    }
    for (auto& v : kernel) v /= sum;
    return kernel;
}

// Separable Gaussian blur on float buffer
void gaussian_blur(std::vector<float>& data, std::vector<float>& temp, int w, int h,
                   int half_size) {
    if (half_size <= 0) return;

    auto kernel = make_gaussian_kernel(half_size);
    int ksize = 2 * half_size + 1;

    // Horizontal pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0F;
                for (int k = 0; k < ksize; ++k) {
                    int sx = std::clamp(x + k - half_size, 0, w - 1);
                    sum += data[y * w + sx] * kernel[k];
                }
                temp[y * w + x] = sum;
            }
        }
    });

    // Vertical pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0F;
                for (int k = 0; k < ksize; ++k) {
                    int sy = std::clamp(y + k - half_size, 0, h - 1);
                    sum += temp[sy * w + x] * kernel[k];
                }
                data[y * w + x] = sum;
            }
        }
    });
}

void convert_cleaned_to_safe_zone(const std::vector<uint8_t>& cleaned,
                                  std::vector<float>& safe_zone, int w, int h) {
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(w);
            for (int x = 0; x < w; ++x) {
                size_t index = row_offset + static_cast<size_t>(x);
                safe_zone[index] = cleaned[index] / 255.0F;
            }
        }
    });
}

void apply_safe_zone(Image alpha, const std::vector<float>& safe_zone) {
    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            size_t row_offset = static_cast<size_t>(y) * static_cast<size_t>(alpha.width);
            for (int x = 0; x < alpha.width; ++x) {
                size_t index = row_offset + static_cast<size_t>(x);
                alpha.data[index] *= safe_zone[index];
            }
        }
    });
}

}  // anonymous namespace

void despeckle(Image alpha, int area_threshold, DespeckleState& state, int dilation,
               int blur_size) {
    if (alpha.empty() || area_threshold <= 0) return;

    int w = alpha.width;
    int h = alpha.height;
    int n = w * h;

    // Step 1: Threshold alpha at 0.5 to binary mask
    state.mask.resize(n);
    const MaskSummary summary = threshold_mask(alpha, state.mask);
    if (summary.foreground_count == n) {
        return;
    }
    if (summary.foreground_count == 0) {
        zero_alpha(alpha);
        return;
    }

    // Step 2: Find connected components and filter by area
    state.cleaned.resize(n);
    filter_components_rle(state.mask, state.cleaned, w, h, area_threshold);

    // Step 3: Dilate with elliptical kernel
    state.temp_mask.resize(n);
    dilate_binary(state.cleaned, state.temp_mask, w, h, dilation);

    // Step 4: Gaussian blur for smooth edges
    state.safe_zone.resize(n);
    convert_cleaned_to_safe_zone(state.cleaned, state.safe_zone, w, h);

    state.blur_temp.resize(n);
    gaussian_blur(state.safe_zone, state.blur_temp, w, h, blur_size);

    // Step 5: Multiply original alpha by safe zone
    apply_safe_zone(alpha, state.safe_zone);
}

}  // namespace corridorkey
// NOLINTEND(readability-identifier-length,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-math-missing-parentheses,cppcoreguidelines-avoid-magic-numbers,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,modernize-use-auto,cppcoreguidelines-pro-bounds-constant-array-index,modernize-use-ranges,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
