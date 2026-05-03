#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "ofx_frame_cache.hpp"
#include "ofx_logging.hpp"
#include "ofx_shared.hpp"

namespace corridorkey::ofx {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast,readability-function-size,readability-function-cognitive-complexity,modernize-use-designated-initializers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
//
// ofx_plugin.cpp tidy-suppression rationale.
//
// The OpenFX 1.4 C ABI mandates a fixed set of TU-local globals (host,
// suite pointers, plugin descriptor) that the host queries through
// OfxSetHost / OfxGetPlugin. The host hands actions in as a void*
// handle that the spec requires the plugin to interpret as the typed
// OfxImageEffectHandle - the reinterpret_cast plus const_cast pair is
// the canonical OFX dispatcher form. plugin_main_entry's size and
// branching reflects the action-dispatch table the spec defines, not
// accidental complexity. The g_plugin POD is initialised in the field
// order required by the OfxPlugin struct contract; designated
// initialisers would obscure the spec field order.
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
//
// Host-name capture is metadata only -- it drives help-button routing and
// log breadcrumbs, never the render path. The internal try/catch keeps a
// host whose property suite throws or violates the C ABI from turning Load
// (or any other action that calls this) into kOfxStatFailed. The plugin
// degrades to "unknown host" routing instead of refusing to load.
void capture_host_name() {
    try {
        if (!g_host_name.empty()) {
            return;
        }
        if (g_host == nullptr || g_suites.property == nullptr ||
            g_suites.property->propGetString == nullptr) {
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
    } catch (const std::exception& e) {
        log_message("capture_host_name_exception", e.what());
    } catch (...) {
        log_message("capture_host_name_exception", "Unknown exception while reading kOfxPropName.");
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
    bool fetched_v2 = (message_suite != nullptr);
    if (message_suite == nullptr) {
        message_suite = g_host->fetchSuite(g_host->host, kOfxMessageSuite, 1);
    }
    g_suites.message = static_cast<const OfxMessageSuiteV2*>(message_suite);

    // OFX 1.4 progress suite. Both Nuke 10+ and Resolve 12.5+ advertise
    // OfxProgressSuite (openfx-misc README-hosts.txt). Try V2 first because
    // it accepts a stable messageId for i18n; fall back to V1 otherwise.
    g_suites.progress_v2 = static_cast<const OfxProgressSuiteV2*>(
        g_host->fetchSuite(g_host->host, kOfxProgressSuite, 2));
    g_suites.progress_v1 = static_cast<const OfxProgressSuiteV1*>(
        g_host->fetchSuite(g_host->host, kOfxProgressSuite, 1));
    log_message("fetch_suites",
                std::string("event=ofx_progress_suite v2_fetched=") +
                    (g_suites.progress_v2 != nullptr ? "1" : "0") + " v1_fetched=" +
                    (g_suites.progress_v1 != nullptr ? "1" : "0"));

    // Surface the V2-suite capability so the runtime log answers
    // "did the host actually expose setPersistentMessage" without a
    // debugger session. openfx-misc README-hosts.txt notes that some
    // hosts return a V2 struct with NULL setPersistentMessage /
    // clearPersistentMessage pointers; capturing this once at suite
    // fetch lets us correlate the absence of node alerts with the
    // host's actual capability vector.
    if (g_suites.message == nullptr) {
        log_message("fetch_suites", "event=ofx_message_suite v2_fetched=0 v1_fetched=0 (none)");
    } else {
        const bool has_message = g_suites.message->message != nullptr;
        const bool has_setpersist = g_suites.message->setPersistentMessage != nullptr;
        const bool has_clearpersist = g_suites.message->clearPersistentMessage != nullptr;
        log_message("fetch_suites",
                    std::string("event=ofx_message_suite v2_fetched=") +
                        (fetched_v2 ? "1" : "0") + " has_message=" +
                        (has_message ? "1" : "0") + " has_setPersistentMessage=" +
                        (has_setpersist ? "1" : "0") + " has_clearPersistentMessage=" +
                        (has_clearpersist ? "1" : "0"));
    }

    if (g_suites.property == nullptr || g_suites.image_effect == nullptr ||
        g_suites.parameter == nullptr) {
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

// OFX 1.5 setPersistentMessage / clearPersistentMessage: allowed from any
// plugin action (including kOfxImageEffectActionRender), not gated by the
// paramSetValue threading rule in ofxParam.h:1088. Nuke renders the
// posted message as a node indicator (icon + tooltip) that survives
// between renders until cleared. ofxMessage.h:118-142 (MessageSuiteV2)
// is the spec; openfx-misc/README-hosts.txt confirms Foundry Nuke 17
// supports the V2 suite.
//
// Per the same openfx-misc compatibility note, "Resolve 14 claims to have
// OpenFX message suite V2, but setPersistentMessage is NULL and
// clearPersistentMessage is garbage." Both helpers therefore null-check
// the function pointers individually; on hosts where they are absent
// the call degrades to a no-op and the in-panel display remains the
// only telemetry surface (which is the historical behavior).
void set_persistent_message(const char* message_type, const char* message_id,
                            const char* message, OfxImageEffectHandle effect) {
    if (g_suites.message == nullptr || g_suites.message->setPersistentMessage == nullptr ||
        effect == nullptr || message == nullptr) {
        log_message("set_persistent_message",
                    std::string("skip reason=missing suite_null=") +
                        (g_suites.message == nullptr ? "1" : "0") + " set_fn_null=" +
                        ((g_suites.message != nullptr &&
                          g_suites.message->setPersistentMessage == nullptr)
                             ? "1"
                             : "0") +
                        " effect_null=" + (effect == nullptr ? "1" : "0") + " message_null=" +
                        (message == nullptr ? "1" : "0"));
        return;
    }
    const OfxStatus status =
        g_suites.message->setPersistentMessage(effect, message_type, message_id, "%s", message);
    log_message("set_persistent_message",
                std::string("event=posted type=") + (message_type ? message_type : "(null)") +
                    " id=" + (message_id ? message_id : "(null)") +
                    " status=" + std::to_string(status) +
                    " body_len=" + std::to_string(message ? std::strlen(message) : 0));
}

// ProgressScope implementation. The scope owns one progressStart /
// progressEnd pair per construction; update() forwards progress in [0, 1]
// to the host and returns false on user cancel. The destructor always
// calls progressEnd() so a thrown exception or early return cannot leak
// the modal dialog. ofxProgress.h:17-27 documents Cancel as
// "kOfxStatReplyNo returned during progressUpdate".
ProgressScope::ProgressScope(OfxImageEffectHandle effect, const char* label,
                             const char* message_id)
    : m_effect(effect) {
    if (m_effect == nullptr || label == nullptr) {
        return;
    }
    if (g_suites.progress_v2 != nullptr && g_suites.progress_v2->progressStart != nullptr) {
        const OfxStatus status = g_suites.progress_v2->progressStart(
            m_effect, label, message_id != nullptr ? message_id : "");
        m_use_v2 = true;
        m_started = (status == kOfxStatOK);
        log_message("progress_scope", std::string("event=start version=v2 status=") +
                                          std::to_string(status) + " label=" + label);
        return;
    }
    if (g_suites.progress_v1 != nullptr && g_suites.progress_v1->progressStart != nullptr) {
        const OfxStatus status = g_suites.progress_v1->progressStart(m_effect, label);
        m_use_v2 = false;
        m_started = (status == kOfxStatOK);
        log_message("progress_scope", std::string("event=start version=v1 status=") +
                                          std::to_string(status) + " label=" + label);
        return;
    }
    log_message("progress_scope", "event=start_skipped reason=no_progress_suite");
}

ProgressScope::~ProgressScope() {
    if (!m_started || m_effect == nullptr) {
        return;
    }
    if (m_use_v2 && g_suites.progress_v2 != nullptr &&
        g_suites.progress_v2->progressEnd != nullptr) {
        g_suites.progress_v2->progressEnd(m_effect);
    } else if (!m_use_v2 && g_suites.progress_v1 != nullptr &&
               g_suites.progress_v1->progressEnd != nullptr) {
        g_suites.progress_v1->progressEnd(m_effect);
    }
}

bool ProgressScope::update(double progress) {
    if (!m_started || m_effect == nullptr) {
        return true;
    }
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    OfxStatus status = kOfxStatReplyDefault;
    if (m_use_v2 && g_suites.progress_v2 != nullptr &&
        g_suites.progress_v2->progressUpdate != nullptr) {
        status = g_suites.progress_v2->progressUpdate(m_effect, progress);
    } else if (!m_use_v2 && g_suites.progress_v1 != nullptr &&
               g_suites.progress_v1->progressUpdate != nullptr) {
        status = g_suites.progress_v1->progressUpdate(m_effect, progress);
    }
    return status != kOfxStatReplyNo;
}

void clear_persistent_message(OfxImageEffectHandle effect) {
    if (g_suites.message == nullptr || g_suites.message->clearPersistentMessage == nullptr ||
        effect == nullptr) {
        return;
    }
    const OfxStatus status = g_suites.message->clearPersistentMessage(effect);
    log_message("clear_persistent_message",
                std::string("event=cleared status=") + std::to_string(status));
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
        if (std::strcmp(action, kOfxActionSyncPrivateData) == 0) {
            return sync_private_data(
                reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
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
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-const-cast,readability-function-size,readability-function-cognitive-complexity,modernize-use-designated-initializers,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
