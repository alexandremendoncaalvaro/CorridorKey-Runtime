#include <array>
#include <catch2/catch_all.hpp>
#include <cstdarg>
#include <filesystem>
#include <map>
#include <string>

#include "plugins/ofx/ofx_frame_cache.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"
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

struct FakeEffectProps {
    InstanceData* instance_data = nullptr;
};

struct FakeRoiProps {
    std::array<double, 4> roi{};
    std::map<std::string, std::array<double, 4>> clip_rois;
};

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int, void** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeEffectProps*>(handle);
    *value = props->instance_data;
    return kOfxStatOK;
}

OfxStatus fake_prop_set_pointer(OfxPropertySetHandle handle, const char* name, int, void* value) {
    if (handle == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeEffectProps*>(handle);
    props->instance_data = reinterpret_cast<InstanceData*>(value);
    return kOfxStatOK;
}

OfxStatus fake_get_property_set(OfxImageEffectHandle handle, OfxPropertySetHandle* props) {
    if (props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(handle);
    return kOfxStatOK;
}

int g_clear_persistent_message_count = 0;

OfxStatus fake_clear_persistent_message(void*) {
    ++g_clear_persistent_message_count;
    return kOfxStatOK;
}

OfxStatus fake_prop_get_double_n(OfxPropertySetHandle handle, const char* name, int count,
                                 double* values) {
    if (handle == nullptr || name == nullptr || values == nullptr || count != 4) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxImageEffectPropRegionOfInterest) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeRoiProps*>(handle);
    for (int index = 0; index < 4; ++index) {
        values[index] = props->roi[static_cast<std::size_t>(index)];
    }
    return kOfxStatOK;
}

OfxStatus fake_prop_set_double_n(OfxPropertySetHandle handle, const char* name, int count,
                                 const double* values) {
    if (handle == nullptr || name == nullptr || values == nullptr || count != 4) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeRoiProps*>(handle);
    std::array<double, 4> stored{};
    for (int index = 0; index < 4; ++index) {
        stored[static_cast<std::size_t>(index)] = values[index];
    }
    props->clip_rois[name] = stored;
    return kOfxStatOK;
}

ImageBuffer filled_alpha() {
    ImageBuffer buffer(2, 2, 1);
    std::fill(buffer.view().data.begin(), buffer.view().data.end(), 0.5F);
    return buffer;
}

ImageBuffer filled_foreground() {
    ImageBuffer buffer(2, 2, 3);
    std::fill(buffer.view().data.begin(), buffer.view().data.end(), 0.25F);
    return buffer;
}

}  // namespace

TEST_CASE("begin and end sequence render reset caches without dropping last frame timing",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.cached_signature = 123;
    data.cached_signature_valid = true;
    data.temporal_alpha = filled_alpha();
    data.temporal_foreground = filled_foreground();
    data.temporal_state_valid = true;
    data.temporal_width = 2;
    data.temporal_height = 2;
    data.render_count = 7;
    data.last_frame_ms = 15.0;
    data.avg_frame_ms = 14.0;
    data.frame_time_samples = 3;
    data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 980.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 120.0, 1, 1},
    };

    FakeEffectProps props{.instance_data = &data};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_property_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    REQUIRE(begin_sequence_render(reinterpret_cast<OfxImageEffectHandle>(&props), nullptr) ==
            kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.cached_signature_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.render_count == 0);
    CHECK(data.last_frame_ms == Catch::Approx(15.0));
    CHECK(data.avg_frame_ms == Catch::Approx(14.0));
    CHECK(data.frame_time_samples == 3);
    CHECK(data.last_render_work_origin == LastRenderWorkOrigin::SharedCache);
    REQUIRE(data.last_render_stage_timings.size() == 2);
    CHECK(data.last_render_stage_timings.front().name == "ort_run");
    CHECK(data.last_render_stage_timings.front().total_ms == Catch::Approx(980.0));

    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.temporal_alpha = filled_alpha();
    data.temporal_foreground = filled_foreground();
    data.temporal_state_valid = true;
    data.last_frame_ms = 22.0;
    data.avg_frame_ms = 18.0;
    data.frame_time_samples = 2;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 1800.0, 1, 1},
    };

    REQUIRE(end_sequence_render(reinterpret_cast<OfxImageEffectHandle>(&props), nullptr) ==
            kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.last_frame_ms == 22.0);
    CHECK(data.avg_frame_ms == 18.0);
    CHECK(data.frame_time_samples == 2);
    CHECK(data.last_render_work_origin == LastRenderWorkOrigin::BackendRender);
    REQUIRE(data.last_render_stage_timings.size() == 1);
    CHECK(data.last_render_stage_timings.front().name == "ort_run");
    CHECK(data.last_render_stage_timings.front().total_ms == Catch::Approx(1800.0));

    g_suites.property = previous_property_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("purge caches clears shared and instance caches", "[unit][ofx][regression]") {
    InstanceData data{};
    data.cached_result.alpha = filled_alpha();
    data.cached_result.foreground = filled_foreground();
    data.cached_result_valid = true;
    data.temporal_state_valid = true;
    data.render_count = 4;
    data.last_frame_ms = 11.0;
    data.avg_frame_ms = 10.0;
    data.frame_time_samples = 2;

    FakeEffectProps props{.instance_data = &data};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_property_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    auto previous_cache = std::move(g_frame_cache);
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_frame_cache = std::make_unique<SharedFrameCache>();

    SharedCacheKey key{1, 2, 3, 0};
    auto alpha = filled_alpha();
    auto foreground = filled_foreground();
    std::vector<StageTiming> stage_timings = {
        StageTiming{"ort_run", 1400.0, 1, 1},
    };
    g_frame_cache->store(key, alpha.view(), foreground.view(), stage_timings);

    ImageBuffer retrieved_alpha;
    ImageBuffer retrieved_foreground;
    std::vector<StageTiming> retrieved_stage_timings;
    REQUIRE(g_frame_cache->try_retrieve(key, retrieved_alpha, retrieved_foreground,
                                        &retrieved_stage_timings));
    REQUIRE(retrieved_stage_timings.size() == 1);
    CHECK(retrieved_stage_timings.front().name == "ort_run");
    CHECK(retrieved_stage_timings.front().total_ms == Catch::Approx(1400.0));

    REQUIRE(purge_caches(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);
    CHECK_FALSE(data.cached_result_valid);
    CHECK_FALSE(data.temporal_state_valid);
    CHECK(data.render_count == 0);
    CHECK(data.last_frame_ms == 0.0);
    CHECK(data.avg_frame_ms == 0.0);
    CHECK(data.frame_time_samples == 0);
    CHECK_FALSE(g_frame_cache->try_retrieve(key, retrieved_alpha, retrieved_foreground,
                                            &retrieved_stage_timings));

    g_frame_cache = std::move(previous_cache);
    g_suites.property = previous_property_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("get regions of interest propagates the requested ROI to source clips",
          "[unit][ofx][regression]") {
    FakeRoiProps in_args{.roi = {10.0, 20.0, 110.0, 220.0}, .clip_rois = {}};
    FakeRoiProps out_args{};

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetDoubleN = fake_prop_get_double_n;
    property_suite.propSetDoubleN = fake_prop_set_double_n;

    auto* previous_property_suite = g_suites.property;
    g_suites.property = &property_suite;

    REQUIRE(get_regions_of_interest(nullptr, reinterpret_cast<OfxPropertySetHandle>(&in_args),
                                    reinterpret_cast<OfxPropertySetHandle>(&out_args)) ==
            kOfxStatOK);

    const auto source_it = out_args.clip_rois.find(std::string("OfxImageClipPropRoI_") +
                                                   kOfxImageEffectSimpleSourceClipName);
    REQUIRE(source_it != out_args.clip_rois.end());
    CHECK(source_it->second == in_args.roi);

    const auto hint_it =
        out_args.clip_rois.find(std::string("OfxImageClipPropRoI_") + kClipAlphaHint);
    REQUIRE(hint_it != out_args.clip_rois.end());
    CHECK(hint_it->second == in_args.roi);

    g_suites.property = previous_property_suite;
}

TEST_CASE("is identity remains conservative for CorridorKey output modes",
          "[unit][ofx][regression]") {
    REQUIRE(is_identity(nullptr, nullptr, nullptr) == kOfxStatReplyDefault);
}

// Canonical OFX createInstance contract: the IPC client is initialized lazily
// on first render via ensure_runtime_client, not synchronously inside
// createInstance. ensure_runtime_client must therefore tolerate null inputs
// and missing-binary states without crashing or throwing, so that strict
// hosts (Foundry Nuke) can call createInstance even when the bundle is
// partially staged or the server has not yet been laid down.
TEST_CASE("ensure_runtime_client refuses null instance data without crashing",
          "[unit][ofx][regression]") {
    REQUIRE_FALSE(ensure_runtime_client(nullptr, nullptr));
}

TEST_CASE("ensure_runtime_client reports binary-missing without throwing",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.runtime_server_path =
        std::filesystem::path("nonexistent_runtime_server_path_for_regression.exe");

    REQUIRE_FALSE(ensure_runtime_client(&data, nullptr));
    CHECK(data.runtime_client == nullptr);
    CHECK_FALSE(data.last_error.empty());
}

TEST_CASE("ensure_runtime_client is idempotent when data is null again",
          "[unit][ofx][regression]") {
    // Second call returns false again, no state change.
    REQUIRE_FALSE(ensure_runtime_client(nullptr, nullptr));
    REQUIRE_FALSE(ensure_runtime_client(nullptr, nullptr));
}

// OFX 1.4 spec, ofxParam.h paramSetValue documentation:
// "paramSetValue should only be called from within a ::kOfxActionInstanceChanged
// or interact action." The plugin must therefore defer paramSetValue from
// render-thread actions (Render, BeginSequenceRender, EndSequenceRender,
// IsIdentity, GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded).
namespace {

int g_param_set_value_count = 0;
std::map<OfxParamHandle, std::string> g_param_set_values;

OfxStatus counting_param_set_value(OfxParamHandle, ...) {
    ++g_param_set_value_count;
    return kOfxStatOK;
}

OfxStatus capturing_param_set_value(OfxParamHandle handle, ...) {
    ++g_param_set_value_count;
    va_list args;
    va_start(args, handle);
    const char* value = va_arg(args, const char*);
    va_end(args);
    g_param_set_values[handle] = value != nullptr ? value : "";
    return kOfxStatOK;
}

OfxStatus passthrough_param_get_value(OfxParamHandle, ...) {
    return kOfxStatOK;
}

OfxStatus accept_prop_set_int(OfxPropertySetHandle, const char*, int, int) {
    return kOfxStatOK;
}

OfxStatus accept_param_get_property_set(OfxParamHandle, OfxPropertySetHandle* props) {
    if (props != nullptr) {
        *props = nullptr;
    }
    return kOfxStatOK;
}

OfxParameterSuiteV1 make_counting_parameter_suite() {
    OfxParameterSuiteV1 suite{};
    suite.paramSetValue = counting_param_set_value;
    suite.paramGetValue = passthrough_param_get_value;
    suite.paramGetPropertySet = accept_param_get_property_set;
    return suite;
}

OfxParameterSuiteV1 make_capturing_parameter_suite() {
    OfxParameterSuiteV1 suite{};
    suite.paramSetValue = capturing_param_set_value;
    suite.paramGetValue = passthrough_param_get_value;
    suite.paramGetPropertySet = accept_param_get_property_set;
    return suite;
}

OfxPropertySuiteV1 make_accepting_property_suite() {
    OfxPropertySuiteV1 suite{};
    suite.propSetInt = accept_prop_set_int;
    return suite;
}

// A non-null sentinel so set_string_param_value reaches the parameter suite
// instead of short-circuiting on the null-handle guard.
OfxParamHandle dummy_param_handle() {
    static int sentinel = 0;
    return reinterpret_cast<OfxParamHandle>(&sentinel);
}

struct RuntimeStatusParamSentinels {
    int processing = 0;
    int device = 0;
    int requested_quality = 0;
    int effective_quality = 0;
    int safe_quality_ceiling = 0;
    int artifact = 0;
    int guide_source = 0;
    int path = 0;
    int session = 0;
    int status = 0;
    int timings = 0;
    int backend_work = 0;
    int update_status = 0;
    int open_update_page = 0;
    int include_pre_releases = 0;
};

OfxParamHandle param_handle(int& sentinel) {
    return reinterpret_cast<OfxParamHandle>(&sentinel);
}

void wire_runtime_status_param_handles(InstanceData& data) {
    OfxParamHandle handle = dummy_param_handle();
    data.runtime_processing_param = handle;
    data.runtime_device_param = handle;
    data.runtime_requested_quality_param = handle;
    data.runtime_effective_quality_param = handle;
    data.runtime_safe_quality_ceiling_param = handle;
    data.runtime_artifact_param = handle;
    data.runtime_guide_source_param = handle;
    data.runtime_path_param = handle;
    data.runtime_session_param = handle;
    data.runtime_status_param = handle;
    data.runtime_timings_param = handle;
    data.runtime_backend_work_param = handle;
    data.update_status_param = handle;
    data.open_update_page_param = handle;
    data.include_pre_releases_param = handle;
}

void wire_runtime_status_param_handles(InstanceData& data, RuntimeStatusParamSentinels& handles) {
    data.runtime_processing_param = param_handle(handles.processing);
    data.runtime_device_param = param_handle(handles.device);
    data.runtime_requested_quality_param = param_handle(handles.requested_quality);
    data.runtime_effective_quality_param = param_handle(handles.effective_quality);
    data.runtime_safe_quality_ceiling_param = param_handle(handles.safe_quality_ceiling);
    data.runtime_artifact_param = param_handle(handles.artifact);
    data.runtime_guide_source_param = param_handle(handles.guide_source);
    data.runtime_path_param = param_handle(handles.path);
    data.runtime_session_param = param_handle(handles.session);
    data.runtime_status_param = param_handle(handles.status);
    data.runtime_timings_param = param_handle(handles.timings);
    data.runtime_backend_work_param = param_handle(handles.backend_work);
    data.update_status_param = param_handle(handles.update_status);
    data.open_update_page_param = param_handle(handles.open_update_page);
    data.include_pre_releases_param = param_handle(handles.include_pre_releases);
}

std::string captured_param_value(OfxParamHandle handle) {
    auto it = g_param_set_values.find(handle);
    if (it == g_param_set_values.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

TEST_CASE("Foundry Nuke defers paramSetValue inside Render action", "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    data.in_render = true;

    auto previous_host_name = g_host_name;
    g_host_name = kHostNameNuke;
    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_param_set_value_count = 0;

    update_runtime_panel(&data);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_host_name = previous_host_name;

    CHECK(g_param_set_value_count == 0);
    CHECK(data.runtime_panel_dirty);
}

TEST_CASE("Foundry Nuke defers paramSetValue inside the BeginSequenceRender window",
          "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    data.in_render_sequence = true;

    auto previous_host_name = g_host_name;
    g_host_name = kHostNameNuke;
    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_param_set_value_count = 0;

    update_runtime_panel(&data);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_host_name = previous_host_name;

    CHECK(g_param_set_value_count == 0);
    CHECK(data.runtime_panel_dirty);
}

// DaVinci Resolve has historically tolerated render-thread paramSetValue
// through internal locking. The live runtime panel is the validated Resolve
// feedback path for per-frame timings; strict hosts still defer above.
TEST_CASE("DaVinci Resolve flushes paramSetValue live during render",
          "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    data.in_render = true;
    data.in_render_sequence = true;

    auto previous_host_name = g_host_name;
    g_host_name = kHostNameResolve;
    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_param_set_value_count = 0;

    update_runtime_panel(&data);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_host_name = previous_host_name;

    CHECK(g_param_set_value_count > 0);
    CHECK_FALSE(data.runtime_panel_dirty);
}

TEST_CASE("runtime panel reports failed first bootstrap instead of initializing",
          "[unit][ofx][regression]") {
    InstanceData data{};
    RuntimeStatusParamSentinels handles{};
    wire_runtime_status_param_handles(data, handles);
    data.in_render = true;
    data.in_render_sequence = true;
    data.device.backend = Backend::Auto;
    data.device.name = "Pending runtime server bootstrap";
    data.runtime_panel_state.requested_quality_mode = kQualityPreview;
    data.runtime_panel_state.requested_resolution = 512;
    data.runtime_panel_state.effective_resolution = 0;
    data.runtime_panel_state.artifact_path = "corridorkey_dynamic_blue_fp16.ts";
    data.last_error =
        "Failed to prepare runtime session for Draft (512) using "
        "corridorkey_dynamic_blue_fp16.ts: TorchTRT runtime arm failed: "
        "LoadLibrary corridorkey_torchtrt.dll failed (GetLastError=126)";

    auto previous_host_name = g_host_name;
    g_host_name = kHostNameResolve;
    OfxParameterSuiteV1 parameter_suite = make_capturing_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_param_set_value_count = 0;
    g_param_set_values.clear();

    update_runtime_panel(&data);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_host_name = previous_host_name;

    CHECK_FALSE(data.runtime_panel_dirty);
    CHECK(captured_param_value(data.runtime_processing_param) == "Unavailable");
    CHECK(captured_param_value(data.runtime_device_param) == "Unavailable");
    CHECK(captured_param_value(data.runtime_effective_quality_param) == "Not loaded");
    CHECK(captured_param_value(data.runtime_session_param) == "Unavailable");
    CHECK(captured_param_value(data.runtime_status_param).rfind("Error: ", 0) == 0);
    for (const auto& [handle, value] : g_param_set_values) {
        (void)handle;
        CHECK(value != "Initializing...");
        CHECK(value != "Loading...");
    }
}

TEST_CASE("update_runtime_panel flushes paramSetValue on the main thread",
          "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    // Both flags false — main thread, paramSetValue is permitted.

    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_param_set_value_count = 0;

    update_runtime_panel(&data);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;

    CHECK(g_param_set_value_count > 0);
    CHECK_FALSE(data.runtime_panel_dirty);
}

TEST_CASE("flush_runtime_panel is a no-op when nothing is pending", "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    data.runtime_panel_dirty = false;

    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    auto* previous_parameter = g_suites.parameter;
    g_suites.parameter = &parameter_suite;
    g_param_set_value_count = 0;

    flush_runtime_panel(&data);

    g_suites.parameter = previous_parameter;

    CHECK(g_param_set_value_count == 0);
}

TEST_CASE("sync_private_data drains a deferred render-thread update", "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    // Simulate a render-sequence call that deferred paramSetValue.
    data.runtime_panel_dirty = true;
    data.in_render = false;
    data.in_render_sequence = false;

    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    // sync_private_data resolves the InstanceData via propGetPointer; the
    // canonical accepting property suite stub does not need this so it is
    // wired up locally for this test only.
    property_suite.propGetPointer = fake_prop_get_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    auto* previous_image = g_suites.image_effect;
    auto previous_host_name = g_host_name;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    // Use the strict deferring host (Nuke) so the test exercises the canonical
    // SyncPrivateData drain path. A separate test below covers the Resolve
    // bypass.
    g_host_name = kHostNameNuke;
    g_param_set_value_count = 0;

    FakeEffectProps props{.instance_data = &data};
    REQUIRE(sync_private_data(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;
    g_host_name = previous_host_name;

    CHECK(g_param_set_value_count > 0);
    CHECK_FALSE(data.runtime_panel_dirty);
}

// Regression: Resolve's openfx.plugin host bridge crashed with ACCESS_VIOLATION
// at openfx.plugin+0x235A0 writing into Resolve.exe when the user closed a
// project containing two CorridorKey instances. Each instance's
// sync_private_data fired 13 paramSetValue + InstanceChanged callbacks into the
// bridge while Resolve was already tearing the project down. The render-thread
// deferral that originally motivated the SyncPrivateData drain explicitly
// excludes Resolve, so the flush here is structurally redundant for that host
// and is the only safe thing to remove from the teardown sequence.
TEST_CASE("sync_private_data does not paramSetValue on Resolve hosts",
          "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    // Same precondition as the deferred-drain test: panel marked dirty by a
    // prior render, action runs on the main thread.
    data.runtime_panel_dirty = true;
    data.in_render = false;
    data.in_render_sequence = false;

    OfxParameterSuiteV1 parameter_suite = make_counting_parameter_suite();
    OfxPropertySuiteV1 property_suite = make_accepting_property_suite();
    property_suite.propGetPointer = fake_prop_get_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;
    auto* previous_parameter = g_suites.parameter;
    auto* previous_property = g_suites.property;
    auto* previous_image = g_suites.image_effect;
    auto previous_host_name = g_host_name;
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_host_name = kHostNameResolve;
    g_param_set_value_count = 0;

    FakeEffectProps props{.instance_data = &data};
    REQUIRE(sync_private_data(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;
    g_host_name = previous_host_name;

    // Zero paramSetValue traffic during sync_private_data on Resolve. The
    // dirty bit stays set so the next non-teardown InstanceChanged drains it
    // through the normal path.
    CHECK(g_param_set_value_count == 0);
    CHECK(data.runtime_panel_dirty);
}

// Regression: DaVinci Resolve 20.3.2 closed after project close with a BEX64
// c0000409 / fail-fast 7 in CorridorKey.ofx while dispatching
// OfxActionDestroyInstance. The OFX log stopped at the action entry before
// the persistent-message clear returned, so Resolve teardown must not receive
// that optional host-suite call.
TEST_CASE("destroy_instance does not clear persistent message on Resolve teardown",
          "[unit][ofx][regression]") {
    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;
    OfxMessageSuiteV2 message_suite{};
    message_suite.clearPersistentMessage = fake_clear_persistent_message;

    auto* previous_property = g_suites.property;
    auto* previous_image = g_suites.image_effect;
    auto* previous_message = g_suites.message;
    auto previous_host_name = g_host_name;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_suites.message = &message_suite;
    g_host_name = kHostNameResolve;
    g_clear_persistent_message_count = 0;

    auto* data = new InstanceData();
    FakeEffectProps props{.instance_data = data};
    REQUIRE(destroy_instance(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;
    g_suites.message = previous_message;
    g_host_name = previous_host_name;

    CHECK(g_clear_persistent_message_count == 0);
    CHECK(props.instance_data == nullptr);
}

TEST_CASE("destroy_instance does not clear persistent message on Nuke teardown",
          "[unit][ofx][regression]") {
    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;
    OfxMessageSuiteV2 message_suite{};
    message_suite.clearPersistentMessage = fake_clear_persistent_message;

    auto* previous_property = g_suites.property;
    auto* previous_image = g_suites.image_effect;
    auto* previous_message = g_suites.message;
    auto previous_host_name = g_host_name;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_suites.message = &message_suite;
    g_host_name = kHostNameNuke;
    g_clear_persistent_message_count = 0;

    auto* data = new InstanceData();
    FakeEffectProps props{.instance_data = data};
    REQUIRE(destroy_instance(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;
    g_suites.message = previous_message;
    g_host_name = previous_host_name;

    CHECK(g_clear_persistent_message_count == 0);
    CHECK(props.instance_data == nullptr);
}

TEST_CASE("destroy_instance clears persistent message on generic hosts",
          "[unit][ofx][regression]") {
    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propSetPointer = fake_prop_set_pointer;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;
    OfxMessageSuiteV2 message_suite{};
    message_suite.clearPersistentMessage = fake_clear_persistent_message;

    auto* previous_property = g_suites.property;
    auto* previous_image = g_suites.image_effect;
    auto* previous_message = g_suites.message;
    auto previous_host_name = g_host_name;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_suites.message = &message_suite;
    g_host_name = "com.example.generic-ofx-host";
    g_clear_persistent_message_count = 0;

    auto* data = new InstanceData();
    FakeEffectProps props{.instance_data = data};
    REQUIRE(destroy_instance(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;
    g_suites.message = previous_message;
    g_host_name = previous_host_name;

    CHECK(g_clear_persistent_message_count == 1);
    CHECK(props.instance_data == nullptr);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
