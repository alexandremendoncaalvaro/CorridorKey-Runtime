#pragma once

#include <corridorkey/types.hpp>
#include <corridorkey/version.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMessage.h"
#include "ofxParam.h"
#include "ofxProgress.h"
#include "ofxProperty.h"
#include "ofx_constants.hpp"
#include "ofx_model_selection.hpp"
#include "post_process/alpha_edge.hpp"

#ifdef _WIN32
#define CORRIDORKEY_OFX_EXPORT OfxExport
#elif defined(__GNUC__)
#define CORRIDORKEY_OFX_EXPORT __attribute__((visibility("default")))
#else
#define CORRIDORKEY_OFX_EXPORT
#endif

namespace corridorkey::ofx {

class OfxRuntimeClient;

// The plugin identifier carries the legacy ".resolve" suffix because changing
// it would orphan every existing CorridorKey node in saved DaVinci Resolve
// projects. The string is otherwise opaque to OFX hosts and the plugin runs
// unchanged in any spec-compliant host (Resolve, Foundry Nuke, others).
constexpr const char* kPluginIdentifier = "com.corridorkey.resolve";
constexpr const char* kPluginLabel = "CorridorKey";
constexpr const char* kPluginGroup = "Keying";

// Values that supported hosts report for their kOfxPropName host property
// (the globally unique reverse-DNS string defined by ofxCore.h). Sourced from
// the OpenFX community host reference
// (https://github.com/NatronGitHub/openfx-misc/blob/master/README-hosts.txt).
constexpr const char* kHostNameNuke = "uk.co.thefoundry.nuke";
constexpr const char* kHostNameResolve = "DaVinciResolveLite";

constexpr const char* kClipAlphaHint = "Alpha Hint";
constexpr const char* kClipMatteOutput = "Matte Output";
constexpr const char* kClipForegroundOutput = "Foreground Output";
constexpr const char* kClipCompositeOutput = "Composite Output";

constexpr const char* kParamQualityMode = "quality_mode";
constexpr const char* kParamQualityFallbackMode = "quality_fallback_mode";
constexpr const char* kParamOutputMode = "output_mode";
constexpr const char* kParamRefinementMode = "refinement_mode";
constexpr const char* kParamCoarseResolutionOverride = "coarse_resolution_override";
constexpr const char* kParamInputColorSpace = "input_color_space";
constexpr const char* kParamScreenColor = "screen_color";
constexpr const char* kParamTemporalSmoothing = "temporal_smoothing";
constexpr const char* kParamDespillStrength = "despill_strength";
constexpr const char* kParamSpillMethod = "spill_method";
constexpr const char* kParamAutoDespeckle = "auto_despeckle";
constexpr const char* kParamDespeckleSize = "despeckle_size";
constexpr const char* kParamAlphaBlackPoint = "alpha_black_point";
constexpr const char* kParamAlphaWhitePoint = "alpha_white_point";
constexpr const char* kParamAlphaErode = "alpha_erode";
constexpr const char* kParamAlphaSoftness = "alpha_softness";
constexpr const char* kParamAlphaGamma = "alpha_gamma";
constexpr const char* kParamUpscaleMethod = "upscale_method";
constexpr const char* kParamEnableTiling = "enable_tiling";
constexpr const char* kParamTileOverlap = "tile_overlap";
constexpr const char* kParamSourcePassthrough = "source_passthrough";
constexpr const char* kParamEdgeErode = "edge_erode";
constexpr const char* kParamEdgeBlur = "edge_blur";
constexpr const char* kParamRuntimeProcessing = "runtime_processing";
constexpr const char* kParamRuntimeDevice = "runtime_device";
constexpr const char* kParamRuntimeRequestedQuality = "runtime_requested_quality";
constexpr const char* kParamRuntimeEffectiveQuality = "runtime_effective_quality";
constexpr const char* kParamRuntimeSafeQualityCeiling = "runtime_safe_quality_ceiling";
constexpr const char* kParamRuntimeArtifact = "runtime_artifact";
constexpr const char* kParamRuntimeGuideSource = "runtime_guide_source";
constexpr const char* kParamRuntimePath = "runtime_path";
constexpr const char* kParamRuntimeSession = "runtime_session";
constexpr const char* kParamRuntimeStatus = "runtime_status";
constexpr const char* kParamRuntimeTimings = "runtime_timings";
constexpr const char* kParamRuntimeBackendWork = "runtime_backend_work";
constexpr const char* kParamRenderTimeout = "render_timeout";
constexpr const char* kParamPrepareTimeout = "prepare_timeout";
constexpr const char* kParamOpenStartHereGuide = "open_start_here_guide";
constexpr const char* kParamOpenQualityGuide = "open_quality_guide";
constexpr const char* kParamOpenAlphaHintGuide = "open_alpha_hint_guide";
constexpr const char* kParamOpenRecoverDetailsGuide = "open_recover_details_guide";
constexpr const char* kParamOpenTilingGuide = "open_tiling_guide";
constexpr const char* kParamOpenResolveTutorial = "open_resolve_tutorial";
constexpr const char* kParamOpenTroubleshooting = "open_troubleshooting";
constexpr const char* kParamHelpGroup = "help_group";
constexpr const char* kParamUpdateStatus = "update_status";
constexpr const char* kParamOpenUpdatePage = "open_update_page";
constexpr const char* kParamCheckUpdates = "check_updates";
constexpr const char* kParamIncludePreReleases = "include_pre_releases";
constexpr const char* kParamOpenLogFolder = "open_log_folder";
constexpr const char* kRuntimeStatusStringMode = kOfxParamStringIsSingleLine;
constexpr int kRuntimeStatusEnabled = 0;

struct OfxSuites {
    const OfxPropertySuiteV1* property = nullptr;
    const OfxImageEffectSuiteV1* image_effect = nullptr;
    const OfxParameterSuiteV1* parameter = nullptr;
    const OfxMessageSuiteV2* message = nullptr;
    // Both V1 and V2 of the OFX progress suite are kept side-by-side so the
    // plugin can call the richest API the host advertises. README-hosts.txt
    // confirms Resolve and Nuke both expose at least V1 in their suite list.
    // Either pointer may be null on hosts that omit the optional suite; the
    // ProgressScope helper degrades to a no-op rather than failing the action.
    const OfxProgressSuiteV1* progress_v1 = nullptr;
    const OfxProgressSuiteV2* progress_v2 = nullptr;
};

struct RuntimePanelState {
    int requested_quality_mode = kQualityPreview;
    int requested_resolution = 0;
    int effective_resolution = 0;
    int safe_quality_ceiling_resolution = 0;
    bool cpu_quality_guardrail_active = false;
    std::filesystem::path artifact_path = {};
    bool session_prepared = false;
    std::uint64_t session_ref_count = 0;
};

enum class GuideSourceKind : std::uint8_t {
    Unknown,
    ExternalAlphaHint,
    RoughFallback,
};

enum class RuntimePathKind : std::uint8_t {
    Unknown,
    Direct,
    ArtifactFallback,
    FullModelTiling,
};

enum class LastRenderWorkOrigin : std::uint8_t {
    None,
    BackendRender,
    SharedCache,
    InstanceCache,
};

struct InstanceData {
    OfxImageEffectHandle effect = nullptr;
    OfxImageClipHandle source_clip = nullptr;
    OfxImageClipHandle alpha_hint_clip = nullptr;
    OfxImageClipHandle output_clip = nullptr;
    OfxParamHandle quality_mode_param = nullptr;
    OfxParamHandle quality_fallback_mode_param = nullptr;
    OfxParamHandle output_mode_param = nullptr;
    OfxParamHandle refinement_mode_param = nullptr;
    OfxParamHandle coarse_resolution_override_param = nullptr;
    OfxParamHandle input_color_space_param = nullptr;
    OfxParamHandle screen_color_param = nullptr;
    OfxParamHandle temporal_smoothing_param = nullptr;
    OfxParamHandle despill_param = nullptr;
    OfxParamHandle spill_method_param = nullptr;
    OfxParamHandle despeckle_param = nullptr;
    OfxParamHandle despeckle_size_param = nullptr;
    OfxParamHandle alpha_black_point_param = nullptr;
    OfxParamHandle alpha_white_point_param = nullptr;
    OfxParamHandle alpha_erode_param = nullptr;
    OfxParamHandle alpha_softness_param = nullptr;
    OfxParamHandle alpha_gamma_param = nullptr;
    OfxParamHandle upscale_method_param = nullptr;
    OfxParamHandle enable_tiling_param = nullptr;
    OfxParamHandle tile_overlap_param = nullptr;
    OfxParamHandle source_passthrough_param = nullptr;
    OfxParamHandle edge_erode_param = nullptr;
    OfxParamHandle edge_blur_param = nullptr;
    OfxParamHandle runtime_processing_param = nullptr;
    OfxParamHandle runtime_device_param = nullptr;
    OfxParamHandle runtime_requested_quality_param = nullptr;
    OfxParamHandle runtime_effective_quality_param = nullptr;
    OfxParamHandle runtime_safe_quality_ceiling_param = nullptr;
    OfxParamHandle runtime_artifact_param = nullptr;
    OfxParamHandle runtime_guide_source_param = nullptr;
    OfxParamHandle runtime_path_param = nullptr;
    OfxParamHandle runtime_session_param = nullptr;
    OfxParamHandle runtime_status_param = nullptr;
    OfxParamHandle runtime_timings_param = nullptr;
    OfxParamHandle runtime_backend_work_param = nullptr;
    OfxParamHandle render_timeout_param = nullptr;
    OfxParamHandle prepare_timeout_param = nullptr;
    OfxParamHandle update_status_param = nullptr;
    OfxParamHandle open_update_page_param = nullptr;
    OfxParamHandle include_pre_releases_param = nullptr;
    // No in-class = nullptr initializer for the unique_ptr below: clang/libc++
    // instantiates ~unique_ptr<T>() at the NSDMI site, which then requires
    // complete OfxRuntimeClient and fails with "sizeof to an incomplete type"
    // in TUs that don't include its full definition. unique_ptr default-
    // constructs to nullptr already.
    std::unique_ptr<OfxRuntimeClient> runtime_client;
    std::filesystem::path models_root = {};
    std::filesystem::path model_path = {};
    std::filesystem::path runtime_server_path = {};
    DeviceInfo device = {};
    DeviceInfo preferred_device = {};
    RuntimeCapabilities runtime_capabilities = {};
    int active_quality_mode = kQualityPreview;
    int requested_resolution = 0;
    int active_resolution = 0;
    bool cpu_quality_guardrail_active = false;
    RuntimePanelState runtime_panel_state = {};
    GuideSourceKind last_guide_source = GuideSourceKind::Unknown;
    RuntimePathKind last_runtime_path = RuntimePathKind::Unknown;
    QualityCompileFailureCache quality_compile_failure_cache = {};
    std::uint64_t render_count = 0;
    std::string last_error;
    // Non-fatal status note shown alongside frame timings. Set when the engine fell back to a
    // lower resolution because the requested one failed to compile (e.g. TensorRT 2048 -> 1536).
    std::string last_warning;
    std::string color_management_status;
    double last_frame_ms = 0.0;
    double avg_frame_ms = 0.0;
    std::uint64_t frame_time_samples = 0;
    LastRenderWorkOrigin last_render_work_origin = LastRenderWorkOrigin::None;
    std::vector<StageTiming> last_render_stage_timings;
    // True only during the body of kOfxImageEffectActionRender. Set by
    // RenderScope in ofx_render.cpp. Used to gate paramSetValue chains, which
    // OFX 1.4 / 1.5 restrict to main-thread actions only (strict hosts such as
    // Foundry Nuke 17 crash if paramSetValue is called from a render thread).
    bool in_render = false;
    // True from kOfxImageEffectActionBeginSequenceRender through
    // kOfxImageEffectActionEndSequenceRender. Covers ensure_engine_for_quality
    // and any paramSetValue chains that fire between begin and end of a render
    // sequence (when in_render is briefly false between Render calls).
    bool in_render_sequence = false;
    bool runtime_panel_dirty = false;

    // Last (severity, body) pushed to the OFX message suite via
    // setPersistentMessage. setPersistentMessage replaces the alert bound to
    // the message_id every call, but Nuke 17's Error Console *appends* a
    // line per call rather than coalescing — so calling it every frame with
    // identical content fills the console with duplicates (issue #56 user
    // report). Dedup at our layer: only re-emit when severity or body
    // actually changed since the last call. Empty body means "currently
    // cleared" so the next non-empty call re-emits.
    std::string last_persistent_severity = {};
    std::string last_persistent_body = {};

    FrameResult cached_result = {};
    bool cached_result_valid = false;
    double cached_time = 0.0;
    int cached_width = 0;
    int cached_height = 0;
    std::uint64_t cached_signature = 0;
    bool cached_signature_valid = false;
    InferenceParams cached_params = {};
    std::filesystem::path cached_model_path = {};
    std::vector<StageTiming> cached_render_stage_timings;
    int cached_screen_color = kDefaultScreenColor;
    double cached_alpha_black_point = 0.0;
    double cached_alpha_white_point = 1.0;
    double cached_alpha_erode = 0.0;
    double cached_alpha_softness = 0.0;
    double cached_alpha_gamma = 1.0;
    double cached_temporal_smoothing = kDefaultTemporalSmoothing;

    ImageBuffer temporal_alpha;
    ImageBuffer temporal_foreground;
    bool temporal_state_valid = false;
    double temporal_time = 0.0;
    int temporal_width = 0;
    int temporal_height = 0;

    AlphaEdgeState alpha_edge_state = {};

    // One-shot guard so the "blue requested but dedicated artifact missing"
    // warning fires once per instance instead of per frame. Reset implicitly
    // on plugin reload because InstanceData is recreated.
    bool blue_fallback_warning_logged = false;
};

class SharedFrameCache;

// OFX hosts populate these process-wide singletons exactly once at plugin
// load (OfxSetHost / on_load). They are not meaningfully const because OFX
// hands us raw OfxHost*/suite vtables it owns, and the frame cache is a
// mutable runtime resource shared across all instances. Wrapping each
// extern instead of using a file-level lint-suppress block keeps the
// suppression scoped to the four singletons that require global state.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern OfxHost* g_host;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern OfxSuites g_suites;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern std::unique_ptr<SharedFrameCache> g_frame_cache;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern std::string g_host_name;

bool fetch_suites();
void capture_host_name();

inline bool is_nuke_host_name(std::string_view host_name) {
    return host_name == kHostNameNuke;
}

inline bool is_resolve_host_name(std::string_view host_name) {
    return host_name == kHostNameResolve;
}

inline bool is_nuke_host() {
    return is_nuke_host_name(g_host_name);
}

inline bool is_resolve_host() {
    return is_resolve_host_name(g_host_name);
}

inline std::string select_tutorial_doc(std::string_view host_name) {
    if (is_nuke_host_name(host_name)) {
        return "OFX_NUKE_TUTORIALS.md";
    }
    if (is_resolve_host_name(host_name)) {
        return "OFX_RESOLVE_TUTORIALS.md";
    }
    return "OFX_PANEL_GUIDE.md";
}

inline std::string host_qualified_phrase(std::string_view host_name, const char* base_phrase) {
    if (base_phrase == nullptr) {
        return {};
    }
    std::string result(base_phrase);
    if (is_nuke_host_name(host_name)) {
        result += " in Nuke";
    } else if (is_resolve_host_name(host_name)) {
        result += " in Resolve";
    }
    return result;
}
void post_message(const char* message_type, const char* message, OfxImageEffectHandle effect);

// Persistent node-indicator hooks (OFX MessageSuiteV2). See body in
// ofx_plugin.cpp for the spec citation and Resolve-14 NULL-pointer
// safety note. These exist so render-thread code can surface dynamic
// runtime telemetry on hosts that do not allow render-thread
// paramSetValue (Foundry Nuke 17).
void set_persistent_message(const char* message_type, const char* message_id,
                            const char* message, OfxImageEffectHandle effect);
void clear_persistent_message(OfxImageEffectHandle effect);

// kOfxProgressSuite (V1 + V2) wrapper used during long-running prepare /
// warmup operations such as TensorRT engine compile. Both Foundry Nuke and
// DaVinci Resolve advertise this suite (openfx-misc README-hosts.txt) and
// surface it as a modal "Loading..." dialog with a Cancel button. The
// wrapper degrades to a no-op when the host does not expose the suite or
// when the effect handle is null. ofxProgress.h:17-27 documents that
// plugins performing analysis should "raise the progress monitor in a
// modal manner" and poll for cancellation.
class ProgressScope {
   public:
    ProgressScope(OfxImageEffectHandle effect, const char* label, const char* message_id);
    ProgressScope(const ProgressScope&) = delete;
    ProgressScope& operator=(const ProgressScope&) = delete;
    ~ProgressScope();
    // Returns false when the host has signalled cancel (kOfxStatReplyNo on
    // progressUpdate). Pass progress in [0, 1]. Safe to call when the
    // host does not expose the progress suite (no-op, returns true).
    bool update(double progress);

   private:
    OfxImageEffectHandle m_effect = nullptr;
    bool m_started = false;
    bool m_use_v2 = false;
};

InstanceData* get_instance_data(OfxImageEffectHandle instance);
void set_instance_data(OfxImageEffectHandle instance, InstanceData* data);

// Note: select_quality_artifact is declared in ofx_model_selection.hpp which
// is included above. No forward declaration here.
bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width = 0,
                               int input_height = 0,
                               QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
                               int coarse_resolution_override = 0,
                               RefinementMode refinement_mode = RefinementMode::Auto,
                               std::string_view screen_color = "green");
bool allow_unrestricted_quality_attempt_for_request(const InstanceData& data, int quality_mode,
                                                    const DeviceInfo& requested_device);
std::string requested_quality_runtime_label(int quality_mode, int requested_resolution,
                                            bool cpu_quality_guardrail_active);
bool sync_runtime_panel_session_state(InstanceData* data);
std::string runtime_session_runtime_label(const InstanceData& data);
std::string runtime_status_runtime_label(const InstanceData& data);
std::string runtime_timings_runtime_label(const InstanceData& data);
std::string runtime_backend_work_runtime_label(const InstanceData& data);
std::string runtime_safe_quality_ceiling_runtime_label(const InstanceData& data);
std::string runtime_guide_source_runtime_label(const InstanceData& data);
std::string runtime_path_runtime_label(const InstanceData& data);

// One-line node-indicator summary surfaced through OFX MessageSuiteV2
// setPersistentMessage. The body mirrors the runtime panel telemetry; the
// severity drives the host's coloured node indicator (red on Error,
// yellow on Warning, neutral on Message). Exposed at namespace scope so
// the unit tests can validate the formatting contract without spinning
// up an OFX host.
struct RuntimeNodeSummary {
    std::string body;
    const char* severity = kOfxMessageMessage;
};
RuntimeNodeSummary compose_runtime_node_summary(const InstanceData& data);
void record_frame_timing(InstanceData* data, double elapsed_ms, LastRenderWorkOrigin work_origin);
Result<GuideSourceKind> resolve_alpha_hint_source(Image rgb_view, Image hint_view,
                                                  bool hint_from_clip,
                                                  AlphaHintPolicy alpha_hint_policy);
void update_runtime_panel(InstanceData* data);
void flush_runtime_panel(InstanceData* data);
// Lazy-initializes the out-of-process runtime client on the first render. Must
// not run inside kOfxImageEffectActionCreateInstance: subprocess spawn from
// that action triggers host-side stalls and crashes (Foundry Nuke 17), and
// the canonical OFX createInstance is for handle caching only.
bool ensure_runtime_client(InstanceData* data, OfxImageEffectHandle instance);
OfxStatus instance_changed(OfxImageEffectHandle instance, OfxPropertySetHandle in_args);

OfxStatus on_load();
OfxStatus describe(OfxImageEffectHandle descriptor);
OfxStatus describe_in_context(OfxImageEffectHandle descriptor, const char* context);
OfxStatus create_instance(OfxImageEffectHandle instance);
OfxStatus destroy_instance(OfxImageEffectHandle instance);
// Main-thread action per OFX 1.4 spec. Used by hosts to request that the
// plugin flush its private state to host-visible storage. We use this hook
// to flush any deferred runtime panel paramSetValue chains that accumulated
// during the previous render sequence.
OfxStatus sync_private_data(OfxImageEffectHandle instance);
OfxStatus render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                 OfxPropertySetHandle out_args);
OfxStatus begin_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args);
OfxStatus end_sequence_render(OfxImageEffectHandle instance, OfxPropertySetHandle in_args);
OfxStatus purge_caches(OfxImageEffectHandle instance);
OfxStatus get_regions_of_interest(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                                  OfxPropertySetHandle out_args);
OfxStatus is_identity(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                      OfxPropertySetHandle out_args);
OfxStatus get_clip_preferences(OfxImageEffectHandle instance, OfxPropertySetHandle out_args);
OfxStatus get_output_colourspace(OfxImageEffectHandle instance, OfxPropertySetHandle in_args,
                                 OfxPropertySetHandle out_args);

}  // namespace corridorkey::ofx
