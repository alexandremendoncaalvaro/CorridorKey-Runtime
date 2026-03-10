#include <catch2/catch_all.hpp>
#include <corridorkey/types.hpp>

#include "core/session_cache_policy.hpp"

using namespace corridorkey;

TEST_CASE("optimized model cache is disabled for CoreML sessions", "[unit][session-cache]") {
    REQUIRE(core::use_optimized_model_cache_for_backend(Backend::CPU));
    REQUIRE_FALSE(core::use_optimized_model_cache_for_backend(Backend::CoreML));
    REQUIRE_FALSE(core::use_optimized_model_cache_for_backend(Backend::Auto));
}
