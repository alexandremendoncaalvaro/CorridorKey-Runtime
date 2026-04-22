#include <catch2/catch_all.hpp>
#include <chrono>

#include "app/ofx_session_policy.hpp"

using namespace corridorkey;
using namespace std::chrono_literals;

TEST_CASE("OFX session policy canonicalizes artifact names", "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::canonical_ofx_artifact_name("models/corridorkey_fp16_1536_ctx.onnx") ==
          "corridorkey_fp16_1536_ctx.onnx");
    CHECK(app::detail::canonical_ofx_artifact_name("corridorkey_int8_512.onnx") ==
          "corridorkey_int8_512.onnx");
}

TEST_CASE("OFX session policy destroys zero-ref TensorRT sessions",
          "[unit][ofx][runtime][regression]") {
    CHECK(app::detail::should_destroy_zero_ref_session(Backend::TensorRT));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CPU));
    CHECK_FALSE(app::detail::should_destroy_zero_ref_session(Backend::CUDA));
}

TEST_CASE("Sticky bridge ceiling tightens immediately", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0, cooldown) == 768);
    CHECK(state.ceiling_px == 768);

    // A lower ceiling (worse pressure) supersedes the sticky immediately.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 512, t0 + 1s, cooldown) == 512);
    CHECK(state.ceiling_px == 512);
}

TEST_CASE("Sticky bridge ceiling holds through pressure flicker", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0, cooldown) == 768);

    // Pressure relaxes to 0 but cooldown has not elapsed: stay sticky.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0 + 2s, cooldown) == 768);
    CHECK(state.ceiling_px == 768);

    // Still within cooldown at t+9s: stay sticky.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0 + 9s, cooldown) == 768);
}

TEST_CASE("Sticky bridge ceiling relaxes after cooldown", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0, cooldown) == 768);
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0 + 11s, cooldown) == 0);
    CHECK(state.ceiling_px == 0);
}

TEST_CASE("Sticky bridge ceiling refreshes stamp while pressure persists", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0, cooldown) == 768);
    // Pressure unchanged at t+8s — cooldown should count from here, not from t0.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0 + 8s, cooldown) == 768);
    // At t+17s, 9s after the last observed pressure — still in cooldown.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0 + 17s, cooldown) == 768);
    // At t+19s, 11s after the last observed pressure — cooldown expired.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0 + 19s, cooldown) == 0);
}

TEST_CASE("Sticky bridge ceiling holds against higher-ceiling relaxation", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 512, t0, cooldown) == 512);
    // Pressure nominally relaxes to 768 before cooldown — stay tight at 512.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0 + 3s, cooldown) == 512);
    // After cooldown, adopt the relaxed 768.
    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 768, t0 + 11s, cooldown) == 768);
    CHECK(state.ceiling_px == 768);
}

TEST_CASE("Sticky bridge ceiling passes through zero when never engaged", "[unit][ofx][runtime]") {
    app::detail::StickyBridgeCeilingState state;
    const auto t0 = std::chrono::steady_clock::time_point{};
    const auto cooldown = 10s;

    CHECK(app::detail::resolve_sticky_bridge_ceiling(state, 0, t0, cooldown) == 0);
    CHECK(state.ceiling_px == 0);
}
