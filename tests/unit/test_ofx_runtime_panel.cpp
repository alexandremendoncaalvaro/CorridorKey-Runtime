#include <catch2/catch_all.hpp>
#include <corridorkey/engine.hpp>
#include <corridorkey/types.hpp>
#include <cstring>

#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

using corridorkey::StageTiming;

using namespace corridorkey::ofx;

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

// Persistent node-indicator summary — the contract that mirrors the
// runtime panel telemetry into a one-line OFX MessageSuiteV2 body.
// Foundry Nuke disallows render-thread paramSetValue (ofxParam.h:1088)
// so on Nuke the persistent message is the only live channel for these
// fields. The unit tests below pin the formatting + severity contract
// so a future refactor cannot silently drop fields that the user docs
// reference.
TEST_CASE("compose_runtime_node_summary surfaces idle state",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.last_render_work_origin = LastRenderWorkOrigin::None;

    const auto summary = compose_runtime_node_summary(data);
    CHECK(summary.body == "Loading...");
    CHECK(std::string(summary.severity) == kOfxMessageMessage);
}

TEST_CASE("compose_runtime_node_summary reports backend, device and effective quality on ready state",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 2048;
    data.last_frame_ms = 1300.0;
    StageTiming hot;
    hot.name = "ort_run";
    hot.total_ms = 454.0;
    data.last_render_stage_timings = {hot};
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.render_count = 1;

    const auto summary = compose_runtime_node_summary(data);
    INFO("body=" << summary.body);
    CHECK(summary.body.find("TensorRT") != std::string::npos);
    CHECK(summary.body.find("RTX 3080") != std::string::npos);
    CHECK(summary.body.find("Effective: ") != std::string::npos);
    CHECK(summary.body.find("2048") != std::string::npos);
    CHECK(summary.body.find("Last:") != std::string::npos);
    CHECK(summary.body.find("Hot:") != std::string::npos);
    CHECK(summary.body.find("ort_run") != std::string::npos);
    CHECK(std::string(summary.severity) == kOfxMessageMessage);
}

TEST_CASE("compose_runtime_node_summary escalates severity on warning",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 1024;
    data.last_frame_ms = 850.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.render_count = 1;
    data.last_warning = "Quality fell back to High (1024) for this frame.";

    const auto summary = compose_runtime_node_summary(data);
    INFO("body=" << summary.body);
    CHECK(summary.body.find("Note:") != std::string::npos);
    CHECK(summary.body.find("Quality fell back") != std::string::npos);
    CHECK(std::string(summary.severity) == kOfxMessageWarning);
}

TEST_CASE("compose_runtime_node_summary escalates severity to error and short-circuits other fields",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 2048;
    data.last_frame_ms = 1300.0;
    data.last_render_work_origin = LastRenderWorkOrigin::BackendRender;
    data.render_count = 1;
    data.last_error = "OFX runtime server process exited during startup.";

    const auto summary = compose_runtime_node_summary(data);
    INFO("body=" << summary.body);
    CHECK(summary.body.rfind("Error: ", 0) == 0);
    CHECK(summary.body.find("OFX runtime server process exited during startup.") !=
          std::string::npos);
    // Error path is exclusive — the backend/device/timing chain is not
    // mixed in so the user sees the actionable message immediately.
    CHECK(summary.body.find("Effective:") == std::string::npos);
    CHECK(summary.body.find("Last:") == std::string::npos);
    CHECK(std::string(summary.severity) == kOfxMessageError);
}

TEST_CASE("compose_runtime_node_summary annotates instance/shared cache hits",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 2048;
    data.last_frame_ms = 12.0;
    data.render_count = 5;

    SECTION("shared cache hit") {
        data.last_render_work_origin = LastRenderWorkOrigin::SharedCache;
        const auto summary = compose_runtime_node_summary(data);
        CHECK(summary.body.find("Shared cache hit") != std::string::npos);
    }

    SECTION("instance cache hit") {
        data.last_render_work_origin = LastRenderWorkOrigin::InstanceCache;
        const auto summary = compose_runtime_node_summary(data);
        CHECK(summary.body.find("Instance cache hit") != std::string::npos);
    }
}

// Pins the dedup contract that update_runtime_node_indicator depends on:
// the (severity, body) pair returned by compose_runtime_node_summary is
// stable when InstanceData fields that don't affect the indicator change.
// If this regresses, every render frame would push setPersistentMessage
// even when the surface message is identical, refilling Nuke's Error
// Console line-by-line (the v0.8.2-win.x failure mode behind issue #56's
// follow-up "screenshot of console full of duplicate WARNINGs").
TEST_CASE("compose_runtime_node_summary is stable under non-display field churn",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 1024;
    data.last_frame_ms = 100.0;
    data.render_count = 1;

    const auto initial = compose_runtime_node_summary(data);

    // render_count and frame_time_samples advance every frame but are
    // not surfaced by the body. The dedup at update_runtime_node_indicator
    // only re-emits when the body or severity literally differs, so these
    // fields must NOT mutate the summary payload.
    data.render_count = 99;
    data.frame_time_samples = 99;

    const auto after = compose_runtime_node_summary(data);

    CHECK(after.body == initial.body);
    CHECK(std::string(after.severity) == std::string(initial.severity));
}

// Pins the (severity, body) split: a state that warrants only a neutral
// info message must not escalate the host-side node indicator. Foundry
// Nuke colours the node thumbnail by severity (red = error, yellow =
// warning, neutral = message); a regression that always returned
// kOfxMessageWarning would paint every node yellow regardless of state.
TEST_CASE("compose_runtime_node_summary keeps neutral severity on healthy state",
          "[unit][ofx][runtime][panel]") {
    InstanceData data{};
    data.device.backend = corridorkey::Backend::TensorRT;
    data.device.name = "NVIDIA GeForce RTX 3080";
    data.runtime_panel_state.effective_resolution = 1024;
    data.last_frame_ms = 100.0;
    data.render_count = 1;

    REQUIRE(data.last_error.empty());
    REQUIRE(data.last_warning.empty());

    const auto summary = compose_runtime_node_summary(data);
    CHECK(std::string(summary.severity) == kOfxMessageMessage);
}

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
