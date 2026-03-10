#include <catch2/catch_all.hpp>
#include <corridorkey/types.hpp>

#include "core/session_policy.hpp"

using namespace corridorkey;

TEST_CASE("session policy uses backend-aware intra-op threads", "[unit][inference]") {
    REQUIRE(core::intra_op_threads_for_backend(Backend::CPU) == 0);
    REQUIRE(core::intra_op_threads_for_backend(Backend::CoreML) == 1);
    REQUIRE(core::intra_op_threads_for_backend(Backend::TensorRT) == 1);
    REQUIRE(core::intra_op_threads_for_backend(Backend::DirectML) == 1);
}
