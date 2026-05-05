#include <catch2/catch_all.hpp>

#include "core/pinned_buffer.hpp"

using namespace corridorkey::core;

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

TEST_CASE("PinnedBuffer default construct is empty", "[unit]") {
    PinnedBuffer<float> buf;
    REQUIRE(buf.empty());
    REQUIRE(buf.size() == 0);
    REQUIRE(buf.data() == nullptr);
}

TEST_CASE("PinnedBuffer try_allocate returns value or nullopt", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(1024);
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());
    REQUIRE(result->size() == 1024);
    REQUIRE(result->data() != nullptr);
#else
    REQUIRE_FALSE(result.has_value());
#endif
}

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA

TEST_CASE("PinnedBuffer allocation is at least 64-byte aligned", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(512);
    REQUIRE(result.has_value());
    auto addr = reinterpret_cast<uintptr_t>(result->data());
    REQUIRE(addr % 64 == 0);
}

TEST_CASE("PinnedBuffer try_allocate zero size returns empty buffer", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(0);
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE("PinnedBuffer move semantics transfer ownership", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(256);
    REQUIRE(result.has_value());
    float* original_ptr = result->data();

    PinnedBuffer<float> moved = std::move(*result);
    REQUIRE(moved.data() == original_ptr);
    REQUIRE(moved.size() == 256);
    REQUIRE(result->empty());
}

TEST_CASE("PinnedBuffer readable after allocation", "[unit]") {
    constexpr std::size_t kCount = 8;
    auto result = PinnedBuffer<float>::try_allocate(kCount);
    REQUIRE(result.has_value());

    float* ptr = result->data();
    for (std::size_t i = 0; i < kCount; ++i) {
        ptr[i] = static_cast<float>(i);
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        REQUIRE(ptr[i] == Catch::Approx(static_cast<float>(i)));
    }
}

#endif

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
