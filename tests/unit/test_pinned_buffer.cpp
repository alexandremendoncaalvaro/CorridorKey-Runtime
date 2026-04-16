#include <catch2/catch_all.hpp>

#include "core/pinned_buffer.hpp"

using namespace corridorkey::core;

TEST_CASE("PinnedBuffer default construct is empty", "[unit]") {
    PinnedBuffer<float> buf;
    REQUIRE(buf.empty());
    REQUIRE(buf.size() == 0);
    REQUIRE(buf.data() == nullptr);
}

TEST_CASE("PinnedBuffer try_allocate returns value or nullopt", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(1024);
#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->empty());
    REQUIRE(result->size() == 1024);
    REQUIRE(result->data() != nullptr);
#else
    REQUIRE_FALSE(result.has_value());
#endif
}

#if defined(CORRIDORKEY_HAS_CUDA) && CORRIDORKEY_HAS_CUDA

TEST_CASE("PinnedBuffer allocation is at least 64-byte aligned", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(512);
    REQUIRE(result.has_value());
    auto addr = reinterpret_cast<uintptr_t>(result->data());
    REQUIRE(addr % 64 == 0);
}

TEST_CASE("PinnedBuffer try_allocate zero size returns empty buffer", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(0);
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE("PinnedBuffer move semantics transfer ownership", "[unit]") {
    auto result = PinnedBuffer<float>::try_allocate(256);
    REQUIRE(result.has_value());
    float* original_ptr = result->data();

    PinnedBuffer<float> moved = std::move(*result);
    REQUIRE(moved.data() == original_ptr);
    REQUIRE(moved.size() == 256);
    REQUIRE(result->empty());
}

TEST_CASE("PinnedBuffer readable after allocation", "[unit]") {
    constexpr std::size_t kCount = 8;
    auto result = PinnedBuffer<float>::try_allocate(kCount);
    REQUIRE(result.has_value());

    float* ptr = result->data();
    for (std::size_t i = 0; i < kCount; ++i) {
        ptr[i] = static_cast<float>(i);
    }
    for (std::size_t i = 0; i < kCount; ++i) {
        REQUIRE(ptr[i] == Catch::Approx(static_cast<float>(i)));
    }
}

#endif
