#include <catch2/catch_all.hpp>

#include <corridorkey/engine.hpp>
#include <cstring>

#include "plugins/ofx/ofx_shared.hpp"
#include "plugins/ofx/ofx_runtime_client.hpp"

using namespace corridorkey::ofx;

TEST_CASE("runtime panel updates are deferred during render", "[unit][ofx][regression]") {
    InstanceData data{};
    data.in_render = true;
    data.runtime_panel_dirty = false;

    update_runtime_panel(&data);

    REQUIRE(data.runtime_panel_dirty);

    data.in_render = false;
    update_runtime_panel(&data);

    REQUIRE_FALSE(data.runtime_panel_dirty);
}

namespace {
struct FakeProps {
    std::string change_reason;
};

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int,
                                void** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropInstanceData) != 0) {
        return kOfxStatErrBadHandle;
    }
    *value = handle;
    return kOfxStatOK;
}

OfxStatus fake_get_property_set(OfxImageEffectHandle handle, OfxPropertySetHandle* props) {
    if (props == nullptr) {
        return kOfxStatErrBadHandle;
    }
    *props = reinterpret_cast<OfxPropertySetHandle>(handle);
    return kOfxStatOK;
}

OfxStatus fake_prop_get_string(OfxPropertySetHandle handle, const char* name, int,
                               char** value) {
    if (handle == nullptr || value == nullptr || name == nullptr) {
        return kOfxStatErrBadHandle;
    }
    if (std::strcmp(name, kOfxPropChangeReason) != 0) {
        return kOfxStatErrBadHandle;
    }
    auto* props = reinterpret_cast<FakeProps*>(handle);
    *value = const_cast<char*>(props->change_reason.c_str());
    return kOfxStatOK;
}
}  // namespace

TEST_CASE("instance_changed ignores plugin-edited callbacks", "[unit][ofx][regression]") {
    InstanceData data{};
    data.runtime_panel_dirty = true;

    FakeProps args{};
    args.change_reason = kOfxChangePluginEdited;

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propGetString = fake_prop_get_string;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    auto status = instance_changed(reinterpret_cast<OfxImageEffectHandle>(&data),
                                   reinterpret_cast<OfxPropertySetHandle>(&args));

    REQUIRE(status == kOfxStatOK);
    REQUIRE(data.runtime_panel_dirty);

    g_suites.property = previous_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("instance_changed flushes pending updates for user edits", "[unit][ofx][regression]") {
    InstanceData data{};
    data.runtime_panel_dirty = true;

    FakeProps args{};
    args.change_reason = kOfxChangeUserEdited;

    OfxPropertySuiteV1 property_suite{};
    property_suite.propGetPointer = fake_prop_get_pointer;
    property_suite.propGetString = fake_prop_get_string;
    OfxImageEffectSuiteV1 image_suite{};
    image_suite.getPropertySet = fake_get_property_set;

    auto* previous_suite = g_suites.property;
    auto* previous_image_suite = g_suites.image_effect;
    g_suites.property = &property_suite;
    g_suites.image_effect = &image_suite;

    auto status = instance_changed(reinterpret_cast<OfxImageEffectHandle>(&data),
                                   reinterpret_cast<OfxPropertySetHandle>(&args));

    REQUIRE(status == kOfxStatOK);
    REQUIRE_FALSE(data.runtime_panel_dirty);

    g_suites.property = previous_suite;
    g_suites.image_effect = previous_image_suite;
}

TEST_CASE("runtime status omits frame timings when a dedicated timings field exists",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.render_count = 4;
    data.last_frame_ms = 12.0;
    data.avg_frame_ms = 10.5;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;

    REQUIRE(runtime_status_runtime_label(data) == "Ready");
    REQUIRE(runtime_timings_runtime_label(data) ==
            "Last render: backend render | Local: 12.0 ms | Avg local: 10.5 ms");
}

TEST_CASE("runtime session label exposes dedicated versus shared sessions",
          "[unit][ofx][regression]") {
    InstanceData data{};

    REQUIRE(runtime_session_runtime_label(data) == "Loading...");

    data.last_error = "Runtime session failed";
    REQUIRE(runtime_session_runtime_label(data) == "Unavailable");

    data.last_error.clear();
    data.runtime_panel_state.session_prepared = true;
    data.runtime_panel_state.session_ref_count = 1;
    REQUIRE(runtime_session_runtime_label(data) == "Dedicated");

    data.runtime_panel_state.session_ref_count = 2;
    REQUIRE(runtime_session_runtime_label(data) == "Shared (2 nodes)");

    data.runtime_panel_state.session_ref_count = 3;
    REQUIRE(runtime_session_runtime_label(data) == "Shared (3 nodes)");
}

TEST_CASE("runtime status still prioritizes errors and warnings while timings stay separate",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_error = "TensorRT compile failed";
    data.last_warning = "Using 1024px fallback";
    data.last_frame_ms = 18.0;
    data.avg_frame_ms = 15.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;

    REQUIRE(runtime_status_runtime_label(data) == "Error: TensorRT compile failed");
    REQUIRE(runtime_timings_runtime_label(data) ==
            "Last render: backend render | Local: 18.0 ms | Avg local: 15.0 ms");

    data.last_error.clear();
    REQUIRE(runtime_status_runtime_label(data) == "Note: Using 1024px fallback");
}

TEST_CASE("runtime timings label exposes cache-backed renders explicitly",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_frame_ms = 3.2;
    data.avg_frame_ms = 4.1;

    data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
    REQUIRE(runtime_timings_runtime_label(data) ==
            "Last render: shared cache | Local: 3.2 ms | Avg local: 4.1 ms");

    data.last_render_work_origin = LastRenderWorkOrigin::InstanceCache;
    REQUIRE(runtime_timings_runtime_label(data) ==
            "Last render: instance cache | Local: 3.2 ms | Avg local: 4.1 ms");
}

TEST_CASE("runtime backend work label summarizes backend renders and cache hits",
          "[unit][ofx][regression]") {
    InstanceData data{};

    REQUIRE(runtime_backend_work_runtime_label(data) == "No backend work recorded");

    data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
    REQUIRE(runtime_backend_work_runtime_label(data) == "No backend work | shared cache hit");

    data.last_render_work_origin = LastRenderWorkOrigin::InstanceCache;
    REQUIRE(runtime_backend_work_runtime_label(data) == "No backend work | instance cache hit");

    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    REQUIRE(runtime_backend_work_runtime_label(data) ==
            "Backend render | stage timings unavailable");

    data.last_render_stage_timings = {
        corridorkey::StageTiming{"frame_prepare_inputs", 16.5, 1, 1},
        corridorkey::StageTiming{"ort_run", 84.5, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 1853.9, 1, 1},
    };
    REQUIRE(runtime_backend_work_runtime_label(data) ==
            "Backend total: 2.0 s | Hotspot: frame_extract_outputs 1.9 s");
}
