#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "ofx_frame_cache.hpp"
#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

OfxHost* g_host = nullptr;
OfxSuites g_suites = {};
std::unique_ptr<SharedFrameCache> g_frame_cache = nullptr;
std::string g_host_name;

static void set_host(OfxHost* host) {
    g_host = host;
}

// Reads the host's kOfxPropName into g_host_name. Per the OpenFX 1.4 spec
// (ofxCore.h, kOfxPropName) this is the globally unique reverse-DNS string
// the host advertises, e.g. "uk.co.thefoundry.nuke" or "DaVinciResolveLite".
// Safe to call multiple times; the second call is a no-op when the name was
// already captured. Must be invoked after fetch_suites() succeeds because it
// depends on the property suite.
void capture_host_name() {
    if (!g_host_name.empty()) {
        return;
    }
    if (g_host == nullptr || g_suites.property == nullptr) {
        return;
    }
    char* host_name_cstr = nullptr;
    const OfxStatus status =
        g_suites.property->propGetString(g_host->host, kOfxPropName, 0, &host_name_cstr);
    if (status == kOfxStatOK && host_name_cstr != nullptr) {
        g_host_name = host_name_cstr;
        log_message("capture_host_name", std::string("kOfxPropName=") + g_host_name);
    } else {
        log_message("capture_host_name", "kOfxPropName unavailable on host property set.");
    }
}

bool fetch_suites() {
    if (g_host == nullptr || g_host->fetchSuite == nullptr) {
        log_message("fetch_suites", "Host or fetchSuite unavailable.");
        return false;
    }

    g_suites.property = static_cast<const OfxPropertySuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxPropertySuite, 1));
    g_suites.image_effect = static_cast<const OfxImageEffectSuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxImageEffectSuite, 1));
    g_suites.parameter = static_cast<const OfxParameterSuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxParameterSuite, 1));

    const void* message_suite = g_host->fetchSuite(g_host->host, kOfxMessageSuite, 2);
    if (message_suite == nullptr) {
        message_suite = g_host->fetchSuite(g_host->host, kOfxMessageSuite, 1);
    }
    g_suites.message = static_cast<const OfxMessageSuiteV2*>(message_suite);

    if (!g_suites.property || !g_suites.image_effect || !g_suites.parameter) {
        log_message("fetch_suites", "Missing required OpenFX suites.");
        return false;
    }
    return true;
}

void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect) {
    if (g_suites.message == nullptr || g_suites.message->message == nullptr) {
        return;
    }

    g_suites.message->message(effect, message_type, "", "%s", message);
}

OfxStatus on_load() {
    log_message("on_load", "Load requested.");
    if (!fetch_suites()) {
        log_message("on_load", "Missing required suites.");
        return kOfxStatErrMissingHostFeature;
    }
    capture_host_name();
    g_frame_cache = std::make_unique<SharedFrameCache>();
    log_message("on_load", "Load successful.");
    return kOfxStatOK;
}

static OfxStatus plugin_main_entry(const char* action, const void* handle,
                                   OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    try {
        // Suppress high-frequency bookkeeping actions from the log. Render fires
        // per frame; BeginInstanceChanged/EndInstanceChanged fire as wrappers
        // around every parameter touch in the UI. The signal lives in the
        // InstanceChanged payload itself (which specific parameter changed) and
        // in the lifecycle events surrounding them. The aggregate dispatch spam
        // made the log unusable for performance triage.
        if (action != nullptr && std::strcmp(action, kOfxImageEffectActionRender) != 0 &&
            std::strcmp(action, kOfxActionBeginInstanceChanged) != 0 &&
            std::strcmp(action, kOfxActionEndInstanceChanged) != 0) {
            log_message("plugin_main_entry", action);
        }
        if (std::strcmp(action, kOfxActionLoad) == 0) {
            return on_load();
        }
        if (std::strcmp(action, kOfxActionDescribe) == 0) {
            return describe(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }
        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            const char* context_value = kOfxImageEffectContextFilter;
            if (in_args != nullptr && g_suites.property != nullptr) {
                char* context = nullptr;
                if (g_suites.property->propGetString(in_args, kOfxImageEffectPropContext, 0,
                                                     &context) == kOfxStatOK &&
                    context != nullptr) {
                    context_value = context;
                }
            }
            return describe_in_context(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), context_value);
        }
        if (std::strcmp(action, kOfxActionCreateInstance) == 0) {
            return create_instance(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
            return destroy_instance(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }
        if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
            return get_clip_preferences(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), out_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionGetOutputColourspace) == 0) {
            return get_output_colourspace(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args,
                out_args);
        }
        if (std::strcmp(action, kOfxActionInstanceChanged) == 0) {
            return instance_changed(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
            return render(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                          in_args, out_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0) {
            return begin_sequence_render(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) {
            return end_sequence_render(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
            return get_regions_of_interest(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args,
                out_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            return is_identity(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                               in_args, out_args);
        }
        if (std::strcmp(action, kOfxActionPurgeCaches) == 0) {
            return purge_caches(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }

        if (std::strcmp(action, kOfxActionUnload) == 0) {
            g_frame_cache.reset();
            close_log();
            return kOfxStatOK;
        }

        return kOfxStatReplyDefault;
    } catch (const std::exception& e) {
        log_message("plugin_main_entry_exception", e.what());
        return kOfxStatFailed;
    } catch (...) {
        log_message("plugin_main_entry_exception", "Unknown exception escaped plugin logic.");
        return kOfxStatFailed;
    }
}

static OfxPlugin g_plugin = {
    kOfxImageEffectPluginApi,  kOfxImageEffectPluginApiVersion, kPluginIdentifier,
    CORRIDORKEY_VERSION_MAJOR, CORRIDORKEY_VERSION_MINOR,       &set_host,
    &plugin_main_entry,
};

}  // namespace corridorkey::ofx

extern "C" {

CORRIDORKEY_OFX_EXPORT OfxStatus OfxSetHost(const OfxHost* host) {
    try {
        corridorkey::ofx::log_message("OfxSetHost",
                                      host == nullptr ? "Host pointer is null." : "Host received.");
        corridorkey::ofx::set_host(const_cast<OfxHost*>(host));
        return kOfxStatOK;
    } catch (...) {
        return kOfxStatFailed;
    }
}

CORRIDORKEY_OFX_EXPORT int OfxGetNumberOfPlugins(void) {
    try {
        corridorkey::ofx::log_message("OfxGetNumberOfPlugins", "Returning 1.");
        return 1;
    } catch (...) {
        return 0;
    }
}

CORRIDORKEY_OFX_EXPORT OfxPlugin* OfxGetPlugin(int nth) {
    try {
        corridorkey::ofx::log_message(
            "OfxGetPlugin", nth == 0 ? "Returning plugin 0." : "Requested invalid index.");
        if (nth == 0) {
            return &corridorkey::ofx::g_plugin;
        }
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

}  // extern "C"
