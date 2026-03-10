#include <catch2/catch_all.hpp>

#include "common/stage_profiler.hpp"

using namespace corridorkey;
using namespace corridorkey::common;

TEST_CASE("stage profiler aggregates repeated samples", "[unit][runtime]") {
    StageProfiler profiler;

    profiler.record(StageTiming{"decode", 4.0, 1, 1});
    profiler.record(StageTiming{"decode", 6.5, 1, 2});
    profiler.record(StageTiming{"infer", 10.0, 1, 1});

    auto timings = profiler.snapshot();

    REQUIRE(timings.size() == 2);
    REQUIRE(timings[0].name == "decode");
    REQUIRE(timings[0].total_ms == Catch::Approx(10.5));
    REQUIRE(timings[0].sample_count == 2);
    REQUIRE(timings[0].work_units == 3);

    REQUIRE(timings[1].name == "infer");
    REQUIRE(timings[1].total_ms == Catch::Approx(10.0));
    REQUIRE(timings[1].sample_count == 1);
    REQUIRE(timings[1].work_units == 1);
}
