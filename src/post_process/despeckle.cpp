#include "despeckle.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq ,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {
namespace {

// Union-Find with path compression and union by rank
struct UnionFind {
    std::vector<int> parent;
    std::vector<int> rank_;

    explicit UnionFind(int n) : parent(n), rank_(n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (rank_[a] < rank_[b]) std::swap(a, b);
        parent[b] = a;
        if (rank_[a] == rank_[b]) ++rank_[a];
    }
};

// 8-connected component labeling using union-find
// Returns label map and area per label
void find_components(const std::vector<uint8_t>& mask, int w, int h,
                     std::vector<int>& labels, std::vector<int>& areas) {
    int n = w * h;
    UnionFind uf(n);
    labels.resize(n, -1);

    // First pass: label and merge
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = y * w + x;
            if (mask[idx] == 0) continue;
            labels[idx] = idx;

            // Check 4 already-visited neighbors (up-left, up, up-right, left)
            constexpr int dy[] = {-1, -1, -1,  0};
            constexpr int dx[] = {-1,  0,  1, -1};
            for (int d = 0; d < 4; ++d) {
                int ny = y + dy[d];
                int nx = x + dx[d];
                if (ny < 0 || ny >= h || nx < 0 || nx >= w) continue;
                int nidx = ny * w + nx;
                if (labels[nidx] >= 0) {
                    uf.unite(idx, nidx);
                }
            }
        }
    }

    // Second pass: flatten labels and compute areas
    std::vector<int> root_map(n, -1);
    int num_labels = 0;
    for (int i = 0; i < n; ++i) {
        if (labels[i] < 0) continue;
        int root = uf.find(i);
        if (root_map[root] < 0) {
            root_map[root] = num_labels++;
        }
        labels[i] = root_map[root];
    }

    areas.assign(num_labels, 0);
    for (int i = 0; i < n; ++i) {
        if (labels[i] >= 0) {
            ++areas[labels[i]];
        }
    }
}

// Generate elliptical structuring element offsets
std::vector<std::pair<int,int>> make_elliptical_kernel(int radius) {
    std::vector<std::pair<int,int>> offsets;
    int ksize = 2 * radius + 1;
    float center = static_cast<float>(radius);
    float r_sq = (center + 0.5f) * (center + 0.5f);

    for (int ky = 0; ky < ksize; ++ky) {
        for (int kx = 0; kx < ksize; ++kx) {
            float dy = static_cast<float>(ky) - center;
            float dx = static_cast<float>(kx) - center;
            if (dy * dy + dx * dx <= r_sq) {
                offsets.emplace_back(ky - radius, kx - radius);
            }
        }
    }
    return offsets;
}

// Morphological dilation with elliptical kernel
void dilate_binary(std::vector<uint8_t>& mask, int w, int h, int radius) {
    if (radius <= 0) return;

    auto offsets = make_elliptical_kernel(radius);
    std::vector<uint8_t> result(mask.size(), 0);

    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            if (result[y * w + x] == 255) continue;
            for (const auto& [dy, dx] : offsets) {
                int ny = y + dy;
                int nx = x + dx;
                if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
                    if (mask[ny * w + nx] == 255) {
                        result[y * w + x] = 255;
                        break;
                    }
                }
            }
        }
    });

    mask = std::move(result);
}

// 1D Gaussian kernel (OpenCV formula: sigma = 0.3*((ksize-1)*0.5 - 1) + 0.8 when sigma=0)
std::vector<float> make_gaussian_kernel(int half_size) {
    int ksize = 2 * half_size + 1;
    float sigma = 0.3f * (static_cast<float>(ksize - 1) * 0.5f - 1.0f) + 0.8f;
    std::vector<float> kernel(ksize);
    float sum = 0.0f;

    for (int i = 0; i < ksize; ++i) {
        float x = static_cast<float>(i - half_size);
        kernel[i] = std::exp(-0.5f * (x * x) / (sigma * sigma));
        sum += kernel[i];
    }
    for (auto& v : kernel) v /= sum;
    return kernel;
}

// Separable Gaussian blur on float buffer
void gaussian_blur(std::vector<float>& data, int w, int h, int half_size) {
    if (half_size <= 0) return;

    auto kernel = make_gaussian_kernel(half_size);
    int ksize = 2 * half_size + 1;
    std::vector<float> temp(data.size());

    // Horizontal pass
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < ksize; ++k) {
                int sx = std::clamp(x + k - half_size, 0, w - 1);
                sum += data[y * w + sx] * kernel[k];
            }
            temp[y * w + x] = sum;
        }
    });

    // Vertical pass
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            for (int k = 0; k < ksize; ++k) {
                int sy = std::clamp(y + k - half_size, 0, h - 1);
                sum += temp[sy * w + x] * kernel[k];
            }
            data[y * w + x] = sum;
        }
    });
}

} // anonymous namespace

void despeckle(Image alpha, int area_threshold, int dilation, int blur_size) {
    if (alpha.empty() || area_threshold <= 0) return;

    int w = alpha.width;
    int h = alpha.height;
    int n = w * h;

    // Step 1: Threshold alpha at 0.5 to binary mask
    std::vector<uint8_t> mask(n);
    for (int i = 0; i < n; ++i) {
        mask[i] = (alpha.data[i] > 0.5f) ? 255 : 0;
    }

    // Step 2: Find connected components and filter by area
    std::vector<int> labels;
    std::vector<int> areas;
    find_components(mask, w, h, labels, areas);

    std::vector<uint8_t> cleaned(n, 0);
    for (int i = 0; i < n; ++i) {
        if (labels[i] >= 0 && areas[labels[i]] >= area_threshold) {
            cleaned[i] = 255;
        }
    }

    // Step 3: Dilate with elliptical kernel
    dilate_binary(cleaned, w, h, dilation);

    // Step 4: Gaussian blur for smooth edges
    std::vector<float> safe_zone(n);
    for (int i = 0; i < n; ++i) {
        safe_zone[i] = cleaned[i] / 255.0f;
    }
    gaussian_blur(safe_zone, w, h, blur_size);

    // Step 5: Multiply original alpha by safe zone
    for (int i = 0; i < n; ++i) {
        alpha.data[i] *= safe_zone[i];
    }
}

} // namespace corridorkey
