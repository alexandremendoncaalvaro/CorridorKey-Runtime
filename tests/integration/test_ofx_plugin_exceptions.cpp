#include <catch2/catch_all.hpp>
#include <stdexcept>
#include <string>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "plugins/ofx/ofx_shared.hpp"

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
}
