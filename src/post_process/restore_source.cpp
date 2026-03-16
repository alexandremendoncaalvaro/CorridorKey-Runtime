#include "restore_source.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/parallel_for.hpp"

namespace corridorkey {

namespace {

// Chamfer distance transform approximation (two-pass, Manhattan-like with diagonal)
// Computes approximate distance from each foreground pixel to the nearest background pixel
void distance_from_edge(const Image& alpha, float threshold, std::vector<float>& dist, int w,
                        int h) {
    constexpr float INF = 1e9f;
    int n = w * h;
    dist.assign(n, INF);

    // Initialize: 0 for edge/background pixels, INF for interior
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (alpha(y, x) < threshold) {
                dist[y * w + x] = 0.0f;
            }
        }
    }

    // Forward pass (top-left to bottom-right)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float d = dist[y * w + x];
            if (x > 0) d = std::min(d, dist[y * w + (x - 1)] + 1.0f);
            if (y > 0) d = std::min(d, dist[(y - 1) * w + x] + 1.0f);
            if (x > 0 && y > 0) d = std::min(d, dist[(y - 1) * w + (x - 1)] + 1.414f);
            if (x < w - 1 && y > 0) d = std::min(d, dist[(y - 1) * w + (x + 1)] + 1.414f);
            dist[y * w + x] = d;
        }
    }

    // Backward pass (bottom-right to top-left)
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            float d = dist[y * w + x];
            if (x < w - 1) d = std::min(d, dist[y * w + (x + 1)] + 1.0f);
            if (y < h - 1) d = std::min(d, dist[(y + 1) * w + x] + 1.0f);
            if (x < w - 1 && y < h - 1) d = std::min(d, dist[(y + 1) * w + (x + 1)] + 1.414f);
            if (x > 0 && y < h - 1) d = std::min(d, dist[(y + 1) * w + (x - 1)] + 1.414f);
            dist[y * w + x] = d;
        }
    }
}

// Detect green contamination using chromaticity (relative proportions).
// Works correctly for both bright and dark pixels near the green screen.
inline bool has_green_contamination(float r, float g, float b) {
    float sum = r + g + b;
    if (sum < 0.01f) return false;  // near-black pixels are safe
    float green_ratio = g / sum;
    // Pure green screen has green_ratio ~0.6-0.8. Neutral gray is ~0.333.
    // Flag anything with green dominance above 0.38 (conservative threshold).
    return green_ratio > 0.38f;
}

}  // namespace

void restore_source_detail(Image foreground, const Image& source, const Image& alpha,
                           float alpha_threshold, int distance_threshold) {
    if (foreground.empty() || source.empty() || alpha.empty()) return;

    int w = foreground.width;
    int h = foreground.height;

    // Compute distance from alpha edge
    std::vector<float> dist;
    distance_from_edge(alpha, alpha_threshold, dist, w, h);

    float dist_min = static_cast<float>(distance_threshold);
    // Smooth blend ramp: fully model at dist_min, fully source at 2x dist_min
    float dist_max = dist_min * 2.0f;

    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float a = alpha(y, x);
                if (a < alpha_threshold) continue;

                float d = dist[y * w + x];
                if (d < dist_min) continue;

                // Check source pixel for green spill - skip contaminated pixels
                float src_r = source(y, x, 0);
                float src_g = source(y, x, 1);
                float src_b = source(y, x, 2);
                if (has_green_contamination(src_r, src_g, src_b)) continue;

                // Smooth blend weight: 0 at dist_min, 1 at dist_max
                float blend = std::clamp((d - dist_min) / (dist_max - dist_min), 0.0f, 1.0f);

                foreground(y, x, 0) += (src_r - foreground(y, x, 0)) * blend;
                foreground(y, x, 1) += (src_g - foreground(y, x, 1)) * blend;
                foreground(y, x, 2) += (src_b - foreground(y, x, 2)) * blend;
            }
        }
    });
}

}  // namespace corridorkey
