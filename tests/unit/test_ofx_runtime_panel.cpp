#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <cstring>

#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

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

OfxStatus fake_prop_get_pointer(OfxPropertySetHandle handle, const char* name, int, void** value) {
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

OfxStatus fake_prop_get_string(OfxPropertySetHandle handle, const char* name, int, char** value) {
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
    data.last_frame_ms = 1500.0;
    data.avg_frame_ms = 1400.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 1200.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 300.0, 1, 1},
    };

    REQUIRE(runtime_status_runtime_label(data) == "Ready");
    REQUIRE(runtime_timings_runtime_label(data) == "1.5 s | Avg: 1.4 s | Hotspot: ort_run 1.2 s");
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
    data.last_frame_ms = 1800.0;
    data.avg_frame_ms = 1600.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 1800.0, 1, 1},
    };

    REQUIRE(runtime_status_runtime_label(data) == "Error: TensorRT compile failed");
    REQUIRE(runtime_timings_runtime_label(data) == "1.8 s | Avg: 1.6 s | Hotspot: ort_run 1.8 s");

    data.last_error.clear();
    REQUIRE(runtime_status_runtime_label(data) == "Note: Using 1024px fallback");
}

TEST_CASE("runtime timings label exposes cache-backed renders explicitly",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_frame_ms = 1100.0;
    data.avg_frame_ms = 1000.0;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 980.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 120.0, 1, 1},
    };

    data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
    REQUIRE(runtime_timings_runtime_label(data) ==
            "1.1 s | Avg: 1.0 s | Shared cache | Hotspot: ort_run 980.0 ms");

    data.last_render_work_origin = LastRenderWorkOrigin::InstanceCache;
    REQUIRE(runtime_timings_runtime_label(data) ==
            "1.1 s | Avg: 1.0 s | Instance cache | Hotspot: ort_run 980.0 ms");
}

TEST_CASE("runtime timings prefer wall time over nested stage totals", "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_frame_ms = 3458.6;
    data.avg_frame_ms = 3400.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"frame_prepare_inputs", 298.6, 1, 1},
        corridorkey::StageTiming{"ort_run", 376.3, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_tensor_materialize", 59.2, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_resize", 2495.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_finalize", 122.5, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 2676.7, 1, 1},
        corridorkey::StageTiming{"post_despill", 4.0, 1, 1},
        corridorkey::StageTiming{"post_premultiply", 12.7, 1, 1},
        corridorkey::StageTiming{"post_composite", 90.4, 1, 1},
    };

    REQUIRE(runtime_timings_runtime_label(data) ==
            "3.5 s | Avg: 3.4 s | Hotspot: frame_extract_outputs_resize 2.5 s");
}

TEST_CASE("runtime timings fallback excludes nested timing double count",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"frame_prepare_inputs", 298.6, 1, 1},
        corridorkey::StageTiming{"ort_run", 376.3, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_tensor_materialize", 59.2, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_resize", 2495.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_finalize", 122.5, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 2676.7, 1, 1},
        corridorkey::StageTiming{"post_despill", 4.0, 1, 1},
        corridorkey::StageTiming{"post_premultiply", 12.7, 1, 1},
        corridorkey::StageTiming{"post_composite", 90.4, 1, 1},
    };

    REQUIRE(runtime_timings_runtime_label(data) ==
            "3.5 s | Avg: 3.5 s | Hotspot: frame_extract_outputs_resize 2.5 s");
}

TEST_CASE("record frame timing keeps cache-hit wall time", "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_render_stage_timings = {
        corridorkey::StageTiming{"ort_run", 376.3, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs_resize", 2495.0, 1, 1},
        corridorkey::StageTiming{"frame_extract_outputs", 2676.7, 1, 1},
    };

    record_frame_timing(&data, 92.0, LastRenderWorkOrigin::SharedCache);

    CHECK(data.last_frame_ms == Catch::Approx(92.0));
    CHECK(data.avg_frame_ms == Catch::Approx(92.0));
    CHECK(data.frame_time_samples == 1);
    CHECK(data.last_render_work_origin == LastRenderWorkOrigin::SharedCache);
    REQUIRE(runtime_timings_runtime_label(data) ==
            "92.0 ms | Avg: 92.0 ms | Shared cache | Hotspot: frame_extract_outputs_resize 2.5 s");
}

TEST_CASE("runtime timings label falls back to preserved frame timing without stage metadata",
          "[unit][ofx][regression]") {
    InstanceData data{};
    data.last_frame_ms = 42.0;
    data.avg_frame_ms = 38.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;

    REQUIRE(runtime_timings_runtime_label(data) == "42.0 ms | Avg: 38.0 ms");
}

TEST_CASE("runtime backend work label summarizes backend renders and cache hits",
          "[unit][ofx][regression]") {
    InstanceData data{};

    REQUIRE(runtime_backend_work_runtime_label(data) == "No backend work recorded");

    data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
    REQUIRE(runtime_backend_work_runtime_label(data) == "Shared cache hit");

    data.last_render_work_origin = LastRenderWorkOrigin::InstanceCache;
    REQUIRE(runtime_backend_work_runtime_label(data) == "Instance cache hit");

    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    REQUIRE(runtime_backend_work_runtime_label(data) == "Backend render");
}

TEST_CASE("runtime panel labels expose safe quality ceiling, guide source, and runtime path",
          "[unit][ofx][regression]") {
    InstanceData data{};

    REQUIRE(runtime_safe_quality_ceiling_runtime_label(data) == "Unknown");
    REQUIRE(runtime_guide_source_runtime_label(data) == "Awaiting render");
    REQUIRE(runtime_path_runtime_label(data) == "Awaiting render");

    data.runtime_panel_state.safe_quality_ceiling_resolution = 1024;
    REQUIRE(runtime_safe_quality_ceiling_runtime_label(data) == "High (1024px)");

    data.last_guide_source = GuideSourceKind::ExternalAlphaHint;
    REQUIRE(runtime_guide_source_runtime_label(data) == "External Alpha Hint");

    data.last_guide_source = GuideSourceKind::RoughFallback;
    REQUIRE(runtime_guide_source_runtime_label(data) == "Rough Fallback");

    data.last_runtime_path = RuntimePathKind::Direct;
    REQUIRE(runtime_path_runtime_label(data) == "Direct");

    data.last_runtime_path = RuntimePathKind::ArtifactFallback;
    REQUIRE(runtime_path_runtime_label(data) == "Artifact Fallback");

    data.last_runtime_path = RuntimePathKind::FullModelTiling;
    REQUIRE(runtime_path_runtime_label(data) == "Full-Model Tiling");
}

TEST_CASE("alpha hint policy prefers external hints and falls back to rough guides",
          "[unit][ofx][regression]") {
    corridorkey::ImageBuffer rgb(2, 1, 3);
    corridorkey::ImageBuffer hint(2, 1, 1);
    auto rgb_view = rgb.view();
    auto hint_view = hint.view();

    rgb_view(0, 0, 0) = 0.1F;
    rgb_view(0, 0, 1) = 0.9F;
    rgb_view(0, 0, 2) = 0.1F;
    rgb_view(0, 1, 0) = 0.9F;
    rgb_view(0, 1, 1) = 0.2F;
    rgb_view(0, 1, 2) = 0.1F;

    std::fill(hint_view.data.begin(), hint_view.data.end(), -1.0F);

    SECTION("external hint wins without mutation") {
        auto result = resolve_alpha_hint_source(rgb_view, hint_view, true,
                                                corridorkey::AlphaHintPolicy::AutoRoughFallback);
        REQUIRE(result.has_value());
        CHECK(static_cast<int>(*result) == static_cast<int>(GuideSourceKind::ExternalAlphaHint));
        CHECK(hint_view(0, 0) == Catch::Approx(-1.0F));
        CHECK(hint_view(0, 1) == Catch::Approx(-1.0F));
    }

    SECTION("missing hint uses rough fallback") {
        auto result = resolve_alpha_hint_source(rgb_view, hint_view, false,
                                                corridorkey::AlphaHintPolicy::AutoRoughFallback);
        REQUIRE(result.has_value());
        CHECK(static_cast<int>(*result) == static_cast<int>(GuideSourceKind::RoughFallback));
        CHECK(hint_view(0, 0) >= 0.0F);
        CHECK(hint_view(0, 0) <= 1.0F);
        CHECK(hint_view(0, 1) >= 0.0F);
        CHECK(hint_view(0, 1) <= 1.0F);
        CHECK(hint_view(0, 0) < hint_view(0, 1));
    }

    SECTION("require external hint returns an explicit error") {
        auto result = resolve_alpha_hint_source(rgb_view, hint_view, false,
                                                corridorkey::AlphaHintPolicy::RequireExternalHint);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == corridorkey::ErrorCode::InvalidParameters);
        CHECK(result.error().message.find("Waiting for Alpha Hint connection.") !=
              std::string::npos);
    }
}
