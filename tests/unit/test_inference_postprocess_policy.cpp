#include <catch2/catch_all.hpp>

#include "core/postprocess_policy.hpp"
#include "post_process/despill.hpp"

using namespace corridorkey;

TEST_CASE("Dedicated blue despill keeps spill removal screen-only",
          "[unit][inference][regression]") {
    CHECK(effective_despill_method({.requested_method = 0, .screen_channel = 1}) ==
          SpillMethod::Average);
    CHECK(effective_despill_method({.requested_method = 1, .screen_channel = 1}) ==
          SpillMethod::DoubleLimit);
    CHECK(effective_despill_method({.requested_method = 2, .screen_channel = 1}) ==
          SpillMethod::Neutral);

    CHECK(effective_despill_method({.requested_method = 0, .screen_channel = 2}) ==
          SpillMethod::ScreenOnly);
    CHECK(effective_despill_method({.requested_method = 1, .screen_channel = 2}) ==
          SpillMethod::ScreenOnly);
    CHECK(effective_despill_method({.requested_method = 2, .screen_channel = 2}) ==
          SpillMethod::ScreenOnly);
}

TEST_CASE("Dedicated blue despill does not warm neutral foreground",
          "[unit][inference][regression]") {
    ImageBuffer foreground_buffer(1, 1, 3);
    auto foreground = foreground_buffer.view();
    foreground(0, 0, 0) = 0.2F;
    foreground(0, 0, 1) = 0.2F;
    foreground(0, 0, 2) = 0.8F;

    despill(foreground, 1.0F,
            effective_despill_method({.requested_method = 0, .screen_channel = 2}), 2);

    CHECK(foreground(0, 0, 0) == Catch::Approx(0.2F));
    CHECK(foreground(0, 0, 1) == Catch::Approx(0.2F));
    CHECK(foreground(0, 0, 2) == Catch::Approx(0.2F));
}
