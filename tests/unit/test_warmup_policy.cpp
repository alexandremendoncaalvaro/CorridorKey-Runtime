#include <catch2/catch_test_macros.hpp>
#include <corridorkey/detail/warmup_policy.hpp>

#include "core/warmup_policy.hpp"

namespace {

using corridorkey::core::should_skip_warmup;
using corridorkey::detail::resolve_warmup_resolution;
using corridorkey::detail::should_run_warmup;

}  // namespace

TEST_CASE("warmup policy resolves target resolution", "[unit][engine][regression]") {
    CHECK(resolve_warmup_resolution(0, 768) == 768);
    CHECK(resolve_warmup_resolution(512, 768) == 512);
    CHECK(resolve_warmup_resolution(2048, 768) == 2048);
}

TEST_CASE("warmup policy only reruns on larger resolutions", "[unit][engine][regression]") {
    std::optional<int> none;
    CHECK(should_run_warmup(512, none));
    CHECK(should_run_warmup(1024, std::optional<int>(512)));
    CHECK_FALSE(should_run_warmup(512, std::optional<int>(1024)));
    CHECK_FALSE(should_run_warmup(1024, std::optional<int>(1024)));
}

TEST_CASE("warmup policy skips large Windows TensorRT warmup", "[unit][engine][regression]") {
#if defined(_WIN32)
    CHECK(should_skip_warmup(corridorkey::Backend::TensorRT, 1536));
    CHECK(should_skip_warmup(corridorkey::Backend::TensorRT, 2048));
    CHECK_FALSE(should_skip_warmup(corridorkey::Backend::TensorRT, 1024));
    CHECK_FALSE(should_skip_warmup(corridorkey::Backend::CPU, 2048));
#else
    CHECK_FALSE(should_skip_warmup(corridorkey::Backend::TensorRT, 2048));
#endif
}
