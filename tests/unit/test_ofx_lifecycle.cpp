#include <array>
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <map>
#include <string>

#include "plugins/ofx/ofx_frame_cache.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

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

OfxStatus counting_param_set_value(OfxParamHandle, ...) {
    ++g_param_set_value_count;
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

}  // namespace

TEST_CASE("Foundry Nuke defers paramSetValue inside Render action",
          "[unit][ofx][regression]") {
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
// through internal locking. The OFX spec endorses host-specific workarounds
// via kOfxPropName, so we take the permissive path on Resolve to preserve
// the long-standing live runtime-status UX. Reference:
// https://openfx.readthedocs.io/en/main/Reference/ofxPropertiesByObject.html
TEST_CASE("DaVinci Resolve flushes paramSetValue live during render",
          "[unit][ofx][regression]") {
    InstanceData data{};
    wire_runtime_status_param_handles(data);
    data.in_render = true;

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

TEST_CASE("flush_runtime_panel is a no-op when nothing is pending",
          "[unit][ofx][regression]") {
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

TEST_CASE("sync_private_data drains a deferred render-thread update",
          "[unit][ofx][regression]") {
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
    g_suites.parameter = &parameter_suite;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;
    g_param_set_value_count = 0;

    FakeEffectProps props{.instance_data = &data};
    REQUIRE(sync_private_data(reinterpret_cast<OfxImageEffectHandle>(&props)) == kOfxStatOK);

    g_suites.parameter = previous_parameter;
    g_suites.property = previous_property;
    g_suites.image_effect = previous_image;

    CHECK(g_param_set_value_count > 0);
    CHECK_FALSE(data.runtime_panel_dirty);
}
