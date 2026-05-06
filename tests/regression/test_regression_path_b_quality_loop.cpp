// Regression: v0.8.0 Path B refactor (commit 1a17033) introduced a placeholder
// DeviceInfo on the .ofx side because device detection now lives in the
// out-of-process runtime server. The placeholder uses Backend::Auto until the
// server reports the real backend on the first prepare_session response. Three
// predicates that drive the candidate-selection loop in ensure_engine_for_quality
// silently misbehave when fed Backend::Auto, and 5/5 ctest passed because no
// test exercised those predicates with the new input shape. The defects only
// surfaced after the user installed v0.8.0 and Resolve hung for 98 seconds
// before reporting "models not loading" (ofx.log, 2026-04-29 09:51-09:53):
//
//   1. backend_matches_request(effective={TorchTRT}, requested={Auto}) returned
//      false, so the loop treated every server response as a backend mismatch
//      and continued past the first dynamic-model success.
//   2. quality_artifact_candidates(backend=Auto, ...) emitted retired ONNX
//      paths. With #1 forcing iteration, the loop reached artifacts that the
//      Windows RTX product path must no longer load.
//   3. The runtime server crashed mid-prepare on unsupported artifacts,
//      exhausting all candidates.
//
// This regression test pins both invariants so the candidate list returned
// to the .ofx loop is safe under the Path B placeholder DeviceInfo.

#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "plugins/ofx/ofx_backend_matching.hpp"
#include "plugins/ofx/ofx_model_selection.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

//
// Test-file tidy-suppression rationale.
//
// Test fixtures legitimately use single-letter loop locals, magic
// numbers (resolution rungs, pixel coordinates, expected error counts),
// std::vector::operator[] on indices the test itself just constructed,
// and Catch2 / aggregate-init styles that pre-date the project's
// tightened .clang-tidy ruleset. The test source is verified
// behaviourally by ctest; converting every site to bounds-checked /
// designated-init / ranges form would obscure intent without changing
// what the tests prove. The same suppressions are documented and
// applied on the src/ tree where the underlying APIs live.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)

void touch_file(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file << "stub";
}

class TempDirGuard {
   public:
    explicit TempDirGuard(const std::string& name)
        : m_path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TempDirGuard() {
        std::filesystem::remove_all(m_path);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return m_path;
    }

   private:
    std::filesystem::path m_path;
};

DeviceInfo path_b_placeholder_device() {
    DeviceInfo device;
    device.backend = Backend::Auto;
    device.name = "Pending runtime server bootstrap";
    device.available_memory_mb = 0;
    return device;
}

DeviceInfo windows_rtx_effective_device() {
    DeviceInfo device;
    device.backend = Backend::TorchTRT;
    device.name = "NVIDIA GeForce RTX 3080";
    device.available_memory_mb = 10240;
    return device;
}

}  // namespace

TEST_CASE(
    "REGRESSION v0.8.0 Path B: placeholder Backend::Auto must short-circuit candidate loop on "
    "first dynamic TorchTRT success and must never expose retired ONNX artifacts",
    "[regression][ofx][path-b]") {
    // Invariant 1 — backend_matches_request must accept any effective backend
    // when the requested backend is the Path B placeholder Auto. Without this,
    // the loop iterates past every server-reported success.
    REQUIRE(backend_matches_request(windows_rtx_effective_device(), path_b_placeholder_device()));

    // Invariant 2 — quality_artifact_candidates must not surface int8 ONNX
    // artifacts when the .ofx asks with the Path B placeholder Auto. The
    // Windows RTX track now selects the single dynamic TorchTRT artifact before
    // engine creation.
    TempDirGuard temp_dir("corridorkey-regression-path-b-quality-loop");
    touch_file(temp_dir.path() / "corridorkey_dynamic_green_fp16.ts");
    touch_file(temp_dir.path() / "corridorkey_int8_512.onnx");
    touch_file(temp_dir.path() / "corridorkey_int8_1024.onnx");

    auto candidates = quality_artifact_candidates(temp_dir.path(), Backend::Auto, kQualityHigh,
                                                  1920, 1080, 10240);

    REQUIRE_FALSE(candidates.empty());
    for (const auto& candidate : candidates) {
        const auto filename = candidate.executable_model_path.filename().string();
        INFO("candidate filename: " << filename);
        REQUIRE(filename == "corridorkey_dynamic_green_fp16.ts");
    }
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
