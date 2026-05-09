#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <string>

#include "plugins/ofx/ofx_runtime_client.hpp"
#include "plugins/ofx/ofx_shared.hpp"

namespace corridorkey::ofx {

TEST_CASE("shared node policy allows color and quality changes on a single node",
          "[unit][ofx][shared-node-policy]") {
    reset_shared_node_policy_for_tests();

    InstanceData data{};
    register_shared_node_policy(&data, kScreenColorGreen, kQualityPreview);

    const SharedNodePolicyResult policy =
        enforce_shared_node_policy(&data, kScreenColorBlue, kQualityMaximum);

    REQUIRE_FALSE(policy.constrained);
    REQUIRE_FALSE(policy.changed);
    REQUIRE(policy.screen_color == kScreenColorBlue);
    REQUIRE(policy.quality_mode == kQualityMaximum);
    REQUIRE_FALSE(data.shared_node_policy_ui_dirty);

    unregister_shared_node_policy(&data);
    reset_shared_node_policy_for_tests();
}

TEST_CASE("shared node policy lets a changed node update color and quality for live nodes",
          "[unit][ofx][shared-node-policy][regression]") {
    reset_shared_node_policy_for_tests();

    InstanceData first{};
    InstanceData second{};
    register_shared_node_policy(&first, kScreenColorGreen, kQualityHigh);
    register_shared_node_policy(&second, kScreenColorBlue, kQualityMaximum);

    const SharedNodePolicyResult policy =
        enforce_shared_node_policy(&second, kScreenColorBlue, kQualityMaximum);

    REQUIRE(policy.constrained);
    REQUIRE_FALSE(policy.changed);
    REQUIRE(policy.screen_color == kScreenColorBlue);
    REQUIRE(policy.quality_mode == kQualityMaximum);
    REQUIRE(first.shared_node_policy_ui_dirty);
    REQUIRE_FALSE(second.shared_node_policy_ui_dirty);

    unregister_shared_node_policy(&second);
    unregister_shared_node_policy(&first);
    reset_shared_node_policy_for_tests();
}

TEST_CASE("shared node policy lets changed inference-affecting parameters update live nodes",
          "[unit][ofx][shared-node-policy][regression]") {
    reset_shared_node_policy_for_tests();

    InstanceData first{};
    InstanceData second{};
    SharedNodePolicyValues first_values;
    first_values.screen_color = kScreenColorGreen;
    first_values.quality_mode = kQualityHigh;
    first_values.source_passthrough_enabled = 1;
    first_values.upscale_method = kUpscaleBilinear;
    first_values.enable_tiling = 0;
    first_values.despill_strength = 0.25;

    SharedNodePolicyValues second_values = first_values;
    second_values.source_passthrough_enabled = 0;
    second_values.upscale_method = kUpscaleLanczos4;
    second_values.enable_tiling = 1;
    second_values.despill_strength = 0.75;

    register_shared_node_policy(&first, first_values);
    register_shared_node_policy(&second, second_values);

    const SharedNodePolicyResult policy = enforce_shared_node_policy(&second, second_values);

    REQUIRE(policy.constrained);
    REQUIRE_FALSE(policy.changed);
    REQUIRE(policy.values.source_passthrough_enabled == 0);
    REQUIRE(policy.values.upscale_method == kUpscaleLanczos4);
    REQUIRE(policy.values.enable_tiling == 1);
    REQUIRE(policy.values.despill_strength == Catch::Approx(0.75));
    REQUIRE(first.shared_node_policy_ui_dirty);

    unregister_shared_node_policy(&second);
    unregister_shared_node_policy(&first);
    reset_shared_node_policy_for_tests();
}

TEST_CASE("shared node policy forces nodes that still need UI sync to the shared values",
          "[unit][ofx][shared-node-policy]") {
    reset_shared_node_policy_for_tests();

    InstanceData first{};
    register_shared_node_policy(&first, kScreenColorBlue, kQualityUltra);

    const SharedNodePolicyResult single_policy =
        enforce_shared_node_policy(&first, kScreenColorBlue, kQualityUltra);
    REQUIRE_FALSE(single_policy.constrained);

    InstanceData second{};
    register_shared_node_policy(&second, kScreenColorGreen, kQualityPreview);
    first.shared_node_policy_ui_dirty = true;
    second.shared_node_policy_ui_dirty = false;
    const SharedNodePolicyResult second_policy =
        enforce_shared_node_policy(&second, kScreenColorGreen, kQualityPreview);

    REQUIRE(second_policy.constrained);
    REQUIRE_FALSE(second_policy.changed);
    REQUIRE(second_policy.screen_color == kScreenColorGreen);
    REQUIRE(second_policy.quality_mode == kQualityPreview);

    const SharedNodePolicyResult first_policy =
        enforce_shared_node_policy(&first, kScreenColorBlue, kQualityUltra);

    REQUIRE(first_policy.constrained);
    REQUIRE(first_policy.changed);
    REQUIRE(first_policy.screen_color == kScreenColorGreen);
    REQUIRE(first_policy.quality_mode == kQualityPreview);
    REQUIRE(first.last_warning.find("Shared nodes use Screen Color Green") != std::string::npos);

    unregister_shared_node_policy(&second);
    unregister_shared_node_policy(&first);
    reset_shared_node_policy_for_tests();
}

TEST_CASE("shared node policy follows the surviving single node after unregister",
          "[unit][ofx][shared-node-policy]") {
    reset_shared_node_policy_for_tests();

    InstanceData first{};
    InstanceData second{};
    register_shared_node_policy(&first, kScreenColorBlue, kQualityUltra);
    register_shared_node_policy(&second, kScreenColorGreen, kQualityPreview);
    unregister_shared_node_policy(&second);

    const SharedNodePolicyResult second_policy =
        enforce_shared_node_policy(&first, kScreenColorBlue, kQualityUltra);

    REQUIRE_FALSE(second_policy.constrained);
    REQUIRE(second_policy.screen_color == kScreenColorBlue);
    REQUIRE(second_policy.quality_mode == kQualityUltra);

    unregister_shared_node_policy(&first);
    reset_shared_node_policy_for_tests();
}

}  // namespace corridorkey::ofx
