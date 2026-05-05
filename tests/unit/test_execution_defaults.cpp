#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

#include "app/runtime_contracts.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

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

void touch_file(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    file << "stub";
}

}  // namespace

TEST_CASE("packaged explicit models rewrite to a coarse sibling when coarse-to-fine is needed",
          "[unit][cli][regression]") {
    TempDirGuard temp_dir("corridorkey-cli-explicit-model");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1536.onnx", params,
                                           DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE(resolved.has_value());
    CHECK(resolved->filename() == "corridorkey_fp16_1024.onnx");
}

TEST_CASE("custom explicit models reject forced coarse-to-fine clearly",
          "[unit][cli][regression]") {
    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved = resolve_model_artifact_for_request(
        "C:/models/custom_keyer.onnx", params, DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("packaged CorridorKey artifacts") != std::string::npos);
}

TEST_CASE("explicit models keep direct behavior when coarse-to-fine is not requested",
          "[unit][cli][regression]") {
    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::Direct;

    auto resolved = resolve_model_artifact_for_request(
        "C:/models/custom_keyer.onnx", params, DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE(resolved.has_value());
    CHECK(*resolved == std::filesystem::path("C:/models/custom_keyer.onnx"));
}

TEST_CASE("coarse-to-fine rejects equal coarse overrides clearly", "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-equal-coarse-override");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;
    params.coarse_resolution_override = 1024;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1024.onnx", params,
                                           DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("smaller than the requested quality") != std::string::npos);
}

TEST_CASE("packaged coarse-to-fine fails clearly when the coarse artifact is missing",
          "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-missing-coarse-artifact");
    touch_file(temp_dir.path() / "corridorkey_fp16_1536.onnx");

    InferenceParams params;
    params.target_resolution = 1536;
    params.requested_quality_resolution = 1536;
    params.quality_fallback_mode = QualityFallbackMode::CoarseToFine;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1536.onnx", params,
                                           DeviceInfo{"RTX 3080", 10240, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::ModelLoadFailed);
    CHECK(resolved.error().message.find("corridorkey_fp16_1024.onnx") != std::string::npos);
}

TEST_CASE("non-auto refinement overrides fail clearly for current packaged artifacts",
          "[unit][runtime][regression]") {
    TempDirGuard temp_dir("corridorkey-runtime-refinement-override");
    touch_file(temp_dir.path() / "corridorkey_fp16_1024.onnx");

    InferenceParams params;
    params.target_resolution = 1024;
    params.requested_quality_resolution = 1024;
    params.quality_fallback_mode = QualityFallbackMode::Direct;
    params.refinement_mode = RefinementMode::Tiled;

    auto resolved =
        resolve_model_artifact_for_request(temp_dir.path() / "corridorkey_fp16_1024.onnx", params,
                                           DeviceInfo{"RTX 4090", 24576, Backend::TensorRT});

    REQUIRE_FALSE(resolved.has_value());
    CHECK(resolved.error().code == ErrorCode::InvalidParameters);
    CHECK(resolved.error().message.find("refinement strategy") != std::string::npos);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
