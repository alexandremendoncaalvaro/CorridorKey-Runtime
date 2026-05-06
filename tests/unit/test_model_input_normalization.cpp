#include <catch2/catch_all.hpp>

#include "core/model_input_normalization.hpp"

using namespace corridorkey;

TEST_CASE("CorridorKey model input normalization matches ImageNet RGB contract",
          "[unit][inference][regression]") {
    CHECK(normalize_corridorkey_rgb(0.485F, ModelRgbChannel::Red) == Catch::Approx(0.0F));
    CHECK(normalize_corridorkey_rgb(0.456F, ModelRgbChannel::Green) == Catch::Approx(0.0F));
    CHECK(normalize_corridorkey_rgb(0.406F, ModelRgbChannel::Blue) == Catch::Approx(0.0F));

    CHECK(normalize_corridorkey_rgb(0.0F, ModelRgbChannel::Red) == Catch::Approx(-0.485F / 0.229F));
    CHECK(normalize_corridorkey_rgb(1.0F, ModelRgbChannel::Green) ==
          Catch::Approx((1.0F - 0.456F) / 0.224F));
    CHECK(normalize_corridorkey_rgb(1.0F, ModelRgbChannel::Blue) ==
          Catch::Approx((1.0F - 0.406F) / 0.225F));
}
