#include "ofx_plugin_descriptors.hpp"

#include <corridorkey/version.hpp>
#include <cstring>

#include "ofxImageEffect.h"
#include "ofx_shared.hpp"

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,modernize-use-designated-initializers)
//
// ofx_plugin_descriptors.cpp tidy-suppression rationale.
//
// The two OfxPlugin PODs below initialise members in the field order
// mandated by the OpenFX C ABI (ofxCore.h). Designated initialisers
// would obscure the spec field order and the OpenFX header declares the
// struct without C99 designated-initialiser support on every supported
// compiler. The descriptor PODs are TU-local globals because OfxGetPlugin
// returns pointers to them and the OpenFX host caches those pointers for
// the lifetime of the loaded binary.

namespace corridorkey::ofx {

namespace {

void set_host(OfxHost* host) {
    g_host = host;
}

OfxStatus plugin_main_entry_green(const char* action, const void* handle,
                                  OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    return plugin_main_entry_dispatch(kPluginIdentifierGreen, action, handle, in_args, out_args);
}

OfxStatus plugin_main_entry_blue(const char* action, const void* handle,
                                 OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    return plugin_main_entry_dispatch(kPluginIdentifierBlue, action, handle, in_args, out_args);
}

OfxPlugin g_plugin_green = {
    kOfxImageEffectPluginApi,  kOfxImageEffectPluginApiVersion, kPluginIdentifierGreen,
    CORRIDORKEY_VERSION_MAJOR, CORRIDORKEY_VERSION_MINOR,       &set_host,
    &plugin_main_entry_green,
};

OfxPlugin g_plugin_blue = {
    kOfxImageEffectPluginApi,  kOfxImageEffectPluginApiVersion, kPluginIdentifierBlue,
    CORRIDORKEY_VERSION_MAJOR, CORRIDORKEY_VERSION_MINOR,       &set_host,
    &plugin_main_entry_blue,
};

}  // namespace

int descriptor_count() {
    return 2;
}

OfxPlugin* descriptor_at(int nth) {
    if (nth == 0) {
        return &g_plugin_green;
    }
    if (nth == 1) {
        return &g_plugin_blue;
    }
    return nullptr;
}

const char* label_for_identifier(const char* identifier) {
    if (identifier != nullptr && std::strcmp(identifier, kPluginIdentifierBlue) == 0) {
        return kPluginLabelBlue;
    }
    return kPluginLabelGreen;
}

}  // namespace corridorkey::ofx

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,modernize-use-designated-initializers)
