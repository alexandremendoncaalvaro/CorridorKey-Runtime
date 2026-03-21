#include <cstdio>
#include <cstring>
#include <exception>

#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

OfxHost* g_host = nullptr;
OfxSuites g_suites = {};

static void set_host(OfxHost* host) {
    g_host = host;
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
    log_message("on_load", "Load successful.");
    return kOfxStatOK;
}

static OfxStatus plugin_main_entry(const char* action, const void* handle,
                                   OfxPropertySetHandle in_args, OfxPropertySetHandle out_args) {
    try {
        if (action != nullptr && std::strcmp(action, kOfxImageEffectActionRender) != 0) {
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
            return create_instance(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
            return destroy_instance(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        }
        if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
            return get_clip_preferences(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), out_args);
        }
        if (std::strcmp(action, kOfxActionInstanceChanged) == 0) {
            return instance_changed(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)),
                                    in_args);
        }
        if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
            return render(reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)), in_args,
                          out_args);
        }

        if (std::strcmp(action, kOfxActionUnload) == 0) {
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
        std::fprintf(stderr, "[OFX] OfxSetHost called\n");
        corridorkey::ofx::log_message("OfxSetHost",
                                      host == nullptr ? "Host pointer is null." : "Host received.");
        corridorkey::ofx::set_host(const_cast<OfxHost*>(host));
        std::fprintf(stderr, "[OFX] OfxSetHost returning kOfxStatOK\n");
        return kOfxStatOK;
    } catch (...) {
        return kOfxStatFailed;
    }
}

CORRIDORKEY_OFX_EXPORT int OfxGetNumberOfPlugins(void) {
    try {
        std::fprintf(stderr, "[OFX] OfxGetNumberOfPlugins called\n");
        corridorkey::ofx::log_message("OfxGetNumberOfPlugins", "Returning 1.");
        std::fprintf(stderr, "[OFX] OfxGetNumberOfPlugins returning 1\n");
        return 1;
    } catch (...) {
        return 0;
    }
}

CORRIDORKEY_OFX_EXPORT OfxPlugin* OfxGetPlugin(int nth) {
    try {
        std::fprintf(stderr, "[OFX] OfxGetPlugin called with index %d\n", nth);
        corridorkey::ofx::log_message("OfxGetPlugin",
                                      nth == 0 ? "Returning plugin 0." : "Requested invalid index.");
        if (nth == 0) {
            std::fprintf(stderr, "[OFX] OfxGetPlugin returning plugin 0\n");
            return &corridorkey::ofx::g_plugin;
        }
        std::fprintf(stderr, "[OFX] OfxGetPlugin returning nullptr\n");
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

}  // extern "C"
