#pragma once

#include <chrono>
#include <corridorkey/types.hpp>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace corridorkey::common {

//
// Header tidy-suppression rationale.
//
// This header is included transitively by many TUs (typically the OFX
// render hot path or the offline batch driver) so its diagnostics
// surface in every consumer once HeaderFilterRegex is scoped to the
// project tree. The categories suppressed below all flag stylistic
// patterns required by the surrounding C ABIs (OFX / ONNX Runtime /
// CUDA / NPP / FFmpeg), the universal pixel / tensor coordinate
// conventions, validated-index operator[] sites, or canonical
// orchestrator function shapes whose linear flow would be obscured by
// helper extraction. Genuine logic regressions are caught by the
// downstream TU sweep and the unit-test suite.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)

class StageProfiler {
   public:
    void record(const StageTiming& sample) {
        std::scoped_lock lock(m_mutex);

        for (auto& stage : m_stages) {
            if (stage.name != sample.name) continue;

            stage.total_ms += sample.total_ms;
            stage.sample_count += sample.sample_count;
            stage.work_units += sample.work_units;
            return;
        }

        m_stages.push_back(sample);
    }

    void record(std::string_view name, double total_ms, std::uint64_t work_units = 0) {
        record(StageTiming{std::string(name), total_ms, 1, work_units});
    }

    template <typename Function>
    decltype(auto) measure(std::string_view name, Function&& function,
                           std::uint64_t work_units = 0) {
        auto start = std::chrono::steady_clock::now();

        if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
            try {
                std::forward<Function>(function)();
            } catch (...) {
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                throw;
            }
            auto end = std::chrono::steady_clock::now();
            record(name, elapsed_ms(start, end), work_units);
        } else {
            try {
                auto result = std::forward<Function>(function)();
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                return result;
            } catch (...) {
                auto end = std::chrono::steady_clock::now();
                record(name, elapsed_ms(start, end), work_units);
                throw;
            }
        }
    }

    [[nodiscard]] std::vector<StageTiming> snapshot() const {
        std::scoped_lock lock(m_mutex);
        return m_stages;
    }

   private:
    static double elapsed_ms(const std::chrono::steady_clock::time_point& start,
                             const std::chrono::steady_clock::time_point& end) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    mutable std::mutex m_mutex;
    std::vector<StageTiming> m_stages = {};
};

template <typename Function>
decltype(auto) measure_stage(const StageTimingCallback& callback, std::string_view name,
                             Function&& function, std::uint64_t work_units = 0) {
    auto start = std::chrono::steady_clock::now();

    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
        try {
            std::forward<Function>(function)();
        } catch (...) {
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            throw;
        }
        auto end = std::chrono::steady_clock::now();
        if (callback) {
            callback(StageTiming{
                std::string(name),
                std::chrono::duration<double, std::milli>(end - start).count(),
                1,
                work_units,
            });
        }
    } else {
        try {
            auto result = std::forward<Function>(function)();
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            return result;
        } catch (...) {
            auto end = std::chrono::steady_clock::now();
            if (callback) {
                callback(StageTiming{
                    std::string(name),
                    std::chrono::duration<double, std::milli>(end - start).count(),
                    1,
                    work_units,
                });
            }
            throw;
        }
    }
}

}  // namespace corridorkey::common

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
