#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace corridorkey::app {

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
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)

CORRIDORKEY_API nlohmann::json summarize_doctor_report(const nlohmann::json& report);
CORRIDORKEY_API nlohmann::json summarize_stage_groups(const std::vector<StageTiming>& timings);

/**
 * @brief Structured result of a job.
 */
struct JobResult {
    bool success;
    std::string message;
    nlohmann::json metadata;
};

/**
 * @brief Definition of a processing job.
 * Decouples CLI arguments from the actual execution logic.
 */
struct JobRequest {
    std::filesystem::path input_path;
    std::filesystem::path hint_path;  // Can be empty for auto-hinting
    std::filesystem::path output_path;
    std::filesystem::path model_path;

    InferenceParams params;
    VideoOutputOptions video_output;
    DeviceInfo device;
};

/**
 * @brief High-level orchestrator that manages the lifecycle of a processing job.
 * This is the primary entry point for the CLI and future TUIs.
 */
class CORRIDORKEY_API JobOrchestrator {
   public:
    /**
     * @brief Execute a job request.
     * Handles file detection (video vs sequence) and engine initialization.
     */
    static Result<void> run(const JobRequest& request, ProgressCallback on_progress = nullptr,
                            JobEventCallback on_event = nullptr);

    /**
     * @brief Get comprehensive hardware and system information as JSON.
     * Used by CLI --json and future GUIs.
     */
    static nlohmann::json get_system_info();

    /**
     * @brief Run a diagnostic check on the system and models.
     */
    static nlohmann::json run_doctor(const std::filesystem::path& models_dir);

    /**
     * @brief Run a performance benchmark.
     */
    static nlohmann::json run_benchmark(const JobRequest& request);

    /**
     * @brief Get the built-in model catalog as stable JSON.
     */
    static nlohmann::json list_models();

    /**
     * @brief Get the built-in preset catalog as stable JSON.
     */
    static nlohmann::json list_presets();

   private:
    static bool is_video_file(const std::filesystem::path& p);
};

}  // namespace corridorkey::app

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)
