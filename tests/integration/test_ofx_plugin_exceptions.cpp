#include <catch2/catch_all.hpp>
#include <stdexcept>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "plugins/ofx/ofx_shared.hpp"

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

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

typedef OfxPlugin* (*OfxGetPluginFunc)(int);
typedef OfxStatus (*OfxSetHostFunc)(const OfxHost*);

extern "C" {
// Mock Property Suite that intentionally throws
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4297)  // function assumed not to throw an exception but does
#endif
OfxStatus mock_propGetString(OfxPropertySetHandle /*properties*/, const char* /*property*/,
                             int /*index*/, char** /*value*/) {
    throw std::runtime_error("Simulated OFX Property Exception");
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

OfxPropertySuiteV1 g_mock_prop_suite = {};
OfxImageEffectSuiteV1 g_mock_image_effect_suite = {};
OfxParameterSuiteV1 g_mock_param_suite = {};

const void* mock_fetchSuite(OfxPropertySetHandle /*host*/, const char* suiteName,
                            int /*suiteVersion*/) {
    if (std::string(suiteName) == kOfxPropertySuite) {
        g_mock_prop_suite.propGetString = mock_propGetString;
        return &g_mock_prop_suite;
    }
    if (std::string(suiteName) == kOfxImageEffectSuite) {
        return &g_mock_image_effect_suite;
    }
    if (std::string(suiteName) == kOfxParameterSuite) {
        return &g_mock_param_suite;
    }
    return nullptr;
}

OfxHost g_mock_host = {nullptr, mock_fetchSuite};

}  // namespace

TEST_CASE("OFX C-API Boundary catches exceptions and returns kOfxStatFailed",
          "[integration][ofx][exceptions]") {
#if defined(CORRIDORKEY_DEPS_HAVE_ASAN)
    // corridorkey_core ships AddressSanitizer under ENABLE_ASAN but this TU
    // does not, so when we dlopen CorridorKey.ofx its ASAN runtime installs
    // after process start and aborts ("Interceptors are not working"). The
    // exception-boundary contract is still covered by non-sanitized presets.
    SKIP(
        "ASAN-instrumented dependency aborts on late dlopen; "
        "coverage ships via non-sanitized preset runs.");
#else

    // Resolve the path to the built DLL
#if defined(_WIN32)
    std::string plugin_path = OFX_PLUGIN_PATH;
    HMODULE handle = LoadLibraryA(plugin_path.c_str());
    REQUIRE(handle != nullptr);
    auto p_OfxGetPlugin = (OfxGetPluginFunc)GetProcAddress(handle, "OfxGetPlugin");
    auto p_OfxSetHost = (OfxSetHostFunc)GetProcAddress(handle, "OfxSetHost");
#else
    std::string plugin_path = OFX_PLUGIN_PATH;
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY);
    REQUIRE(handle != nullptr);
    auto p_OfxGetPlugin = (OfxGetPluginFunc)dlsym(handle, "OfxGetPlugin");
    auto p_OfxSetHost = (OfxSetHostFunc)dlsym(handle, "OfxSetHost");
#endif

    REQUIRE(p_OfxGetPlugin != nullptr);
    REQUIRE(p_OfxSetHost != nullptr);

    // 1. Inject the mocked host
    p_OfxSetHost(&g_mock_host);

    OfxPlugin* plugin = p_OfxGetPlugin(0);
    REQUIRE(plugin != nullptr);

    // 2. Fire the Load Action to populate the g_suites structs
    OfxStatus load_status = plugin->mainEntry(kOfxActionLoad, nullptr, nullptr, nullptr);
    REQUIRE(load_status == kOfxStatOK);

    // 3. Fire an Action that queries the Property Suite
    int dummy_in_args = 42;
    OfxStatus exception_status = plugin->mainEntry(kOfxImageEffectActionDescribeInContext, nullptr,
                                                   (OfxPropertySetHandle)&dummy_in_args, nullptr);

    // 4. Validate that the host application did not crash and gracefully captured kOfxStatFailed
    REQUIRE(exception_status == kOfxStatFailed);

#if defined(_WIN32)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
#endif
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
