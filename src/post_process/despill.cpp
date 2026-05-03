#include "despill.hpp"

#include <algorithm>
#include <numeric>
#include <vector>

#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq,
#else
#define EXEC_POLICY
#endif

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-identifier-length,readability-math-missing-parentheses,readability-function-cognitive-complexity,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// despill tidy-suppression rationale.
//
// post-process pixel-math is OFX render hot path; per CLAUDE.md changes
// here are gated by the phase_8_gpu_prepare 10% regression budget, so we
// suppress diagnostics that would force restructuring without measurable
// safety value. The (a, b, x, y, w, h) names are universal pixel-coord
// and channel-pair conventions; the 0.5F fill split, 1/3 neutral
// average, and 1e-6F divide-by-zero guard are canonical despill
// constants. The screen / other_a / other_b dispatch is one fused
// per-pixel orchestrator whose linear flow would be obscured if the
// switch / spill / neutral-vs-average branches were extracted into
// helpers.
namespace corridorkey {

void despill(Image rgb, float strength, SpillMethod method, int screen_channel) {
    if (rgb.empty() || strength <= 0.0F) return;
    if (screen_channel < 0 || screen_channel > 2) return;

    int h = rgb.height;
    int w = rgb.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    const int other_a = (screen_channel == 0) ? 1 : 0;
    const int other_b = (screen_channel == 2) ? 1 : 2;

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float screen = rgb(y, x, screen_channel);
            float a = rgb(y, x, other_a);
            float b = rgb(y, x, other_b);

            float limit = 0.0F;
            switch (method) {
                case SpillMethod::DoubleLimit:
                    limit = std::max(a, b);
                    break;
                case SpillMethod::Neutral:
                case SpillMethod::Average:
                    limit = (a + b) * 0.5F;
                    break;
            }

            float spill = std::max(0.0F, screen - limit);
            if (spill > 0.0F) {
                float effective_spill = spill * strength;
                float new_screen = screen - effective_spill;
                rgb(y, x, screen_channel) = new_screen;

                if (method == SpillMethod::Neutral) {
                    float gray = (a + new_screen + b) / 3.0F;
                    float fill = effective_spill * 0.5F;
                    rgb(y, x, other_a) = a + fill * (gray / std::max(a, 1e-6F));
                    rgb(y, x, other_b) = b + fill * (gray / std::max(b, 1e-6F));
                } else {
                    rgb(y, x, other_a) = a + effective_spill * 0.5F;
                    rgb(y, x, other_b) = b + effective_spill * 0.5F;
                }
            }
        }
    });
}

}  // namespace corridorkey
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-identifier-length,readability-math-missing-parentheses,readability-function-cognitive-complexity,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
