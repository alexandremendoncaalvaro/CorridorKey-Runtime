#include <catch2/catch_all.hpp>

#include "app/ofx_session_policy.hpp"

using namespace corridorkey;

TEST_CASE("OFX session policy canonicalizes artifact names", "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::canonical_ofx_artifact_name("models/corridorkey_fp16_1536_ctx.onnx") ==
          "corridorkey_fp16_1536_ctx.onnx");
    CHECK(app::detail::canonical_ofx_artifact_name("corridorkey_fp16_512.onnx") ==
          "corridorkey_fp16_512.onnx");
}

TEST_CASE("OFX session policy destroys zero-ref TensorRT sessions",
          "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::should_destroy_zero_ref_session(Backend::TensorRT));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CPU));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CUDA));
}

TEST_CASE("Prewarm resident estimate scales quadratically with shape", "[unit][ofx][runtime]") {
    const auto e512 = app::detail::estimate_mlx_resident_bytes(512);
    const auto e1024 = app::detail::estimate_mlx_resident_bytes(1024);
    const auto e2048 = app::detail::estimate_mlx_resident_bytes(2048);
    // Doubling the shape should quadruple resident bytes.
    CHECK(e1024 == e512 * 4);
    CHECK(e2048 == e1024 * 4);
    // 1024-bridge resident estimate should sit comfortably between the
    // v0.7.6-mac.3 observed active_bytes (~296 MB) and twice that, so the
    // admission check has reasonable headroom built in.
    CHECK(e1024 > 280ULL * 1024 * 1024);
    CHECK(e1024 < 600ULL * 1024 * 1024);
}

TEST_CASE("Prewarm peak-compile estimate is triple the resident estimate", "[unit][ofx][runtime]") {
    for (int shape : {512, 768, 1024, 1536, 2048}) {
        CHECK(app::detail::estimate_mlx_peak_compile_bytes(shape) ==
              app::detail::estimate_mlx_resident_bytes(shape) * 3ULL);
    }
}

TEST_CASE("Admission allows a comfortable working set", "[unit][ofx][runtime]") {
    // 10 GB ceiling, nothing in use: 1024 bridge (~500 MB required with
    // the 1.5x margin) is well inside the envelope.
    app::detail::PrewarmMemorySnapshot snapshot;
    snapshot.recommended_working_set_bytes = 10ULL * 1024 * 1024 * 1024;
    snapshot.mlx_active_bytes = 0;
    snapshot.mlx_cache_bytes = 0;
    CHECK(app::detail::can_admit_session(snapshot, 1024));
    CHECK(app::detail::can_admit_session(snapshot, 1536));
}

TEST_CASE("Admission refuses when ceiling is too low", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    // 1 GB ceiling: a 1024 bridge needs ~500 MB resident * 1.5 margin =
    // ~750 MB, which fits. A 2048 bridge needs ~1.34 GB * 1.5 = 2 GB and
    // must be refused.
    snapshot.recommended_working_set_bytes = 1024ULL * 1024 * 1024;
    CHECK(app::detail::can_admit_session(snapshot, 1024));
    CHECK_FALSE(app::detail::can_admit_session(snapshot, 2048));
}

TEST_CASE("Admission accounts for MLX active allocations", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    snapshot.recommended_working_set_bytes = 2ULL * 1024 * 1024 * 1024;  // 2 GB
    // First, fresh device: 1024 admits.
    CHECK(app::detail::can_admit_session(snapshot, 1024));
    // Now MLX is already holding ~1.66 GB, leaving ~348 MB of headroom.
    // 1024 bridge requires 320 MB resident * 1.5 = 480 MB, which no longer fits.
    snapshot.mlx_active_bytes = 1700ULL * 1024 * 1024;
    CHECK_FALSE(app::detail::can_admit_session(snapshot, 1024));
}

TEST_CASE("Admission discounts MLX cache by half", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    snapshot.recommended_working_set_bytes = 1500ULL * 1024 * 1024;  // 1.5 GB
    // 1 GB of cache: half is reclaimable, effective in-use is 500 MB.
    // 1024 bridge requires ~500 MB * 1.5 = 750 MB; remaining is ~1 GB.
    snapshot.mlx_cache_bytes = 1024ULL * 1024 * 1024;
    CHECK(app::detail::can_admit_session(snapshot, 1024));
}

TEST_CASE("Admission passes when telemetry is unavailable", "[unit][ofx][runtime]") {
    // recommended==0 simulates non-Apple builds or Metal probe failure.
    // We deliberately fall open so those environments do not regress.
    app::detail::PrewarmMemorySnapshot snapshot;
    CHECK(app::detail::can_admit_session(snapshot, 2048));
    CHECK(app::detail::can_admit_session(snapshot, 1024));
}

TEST_CASE("Prewarm decision gates JIT peak against headroom", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    snapshot.recommended_working_set_bytes = 10ULL * 1024 * 1024 * 1024;
    // 1024 peak ~ 1 GB; 10 GB ceiling is plenty.
    CHECK(app::detail::evaluate_prewarm_decision(snapshot, 1024) ==
          app::detail::PrewarmDecision::Prewarm);
    // Tight ceiling rejects prewarm but does not downshift.
    snapshot.recommended_working_set_bytes = 2ULL * 1024 * 1024 * 1024;
    CHECK(app::detail::evaluate_prewarm_decision(snapshot, 2048) ==
          app::detail::PrewarmDecision::SkipInsufficientHeadroom);
}

TEST_CASE("Prewarm decision vetoes during system pressure", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    snapshot.recommended_working_set_bytes = 10ULL * 1024 * 1024 * 1024;
    snapshot.system_pressure_warn_or_critical = true;
    CHECK(app::detail::evaluate_prewarm_decision(snapshot, 1024) ==
          app::detail::PrewarmDecision::SkipSystemPressure);
}

TEST_CASE("Prewarm decision reports unavailable telemetry", "[unit][ofx][runtime]") {
    app::detail::PrewarmMemorySnapshot snapshot;
    // recommended==0 -> we cannot reason about headroom. Caller chooses.
    CHECK(app::detail::evaluate_prewarm_decision(snapshot, 1024) ==
          app::detail::PrewarmDecision::SkipTelemetryUnavailable);
}

TEST_CASE("Prewarm decision labels are stable log tokens", "[unit][ofx][runtime][regression]") {
    // These strings flow into the runtime server log; changing them
    // silently would break log-parsers. Lock them with a regression test.
    CHECK(std::string(app::detail::prewarm_decision_label(app::detail::PrewarmDecision::Prewarm)) ==
          "prewarm");
    CHECK(std::string(app::detail::prewarm_decision_label(
              app::detail::PrewarmDecision::SkipInsufficientHeadroom)) ==
          "skip_insufficient_headroom");
    CHECK(std::string(app::detail::prewarm_decision_label(
              app::detail::PrewarmDecision::SkipSystemPressure)) == "skip_system_pressure");
    CHECK(std::string(app::detail::prewarm_decision_label(
              app::detail::PrewarmDecision::SkipTelemetryUnavailable)) ==
          "skip_telemetry_unavailable");
}
