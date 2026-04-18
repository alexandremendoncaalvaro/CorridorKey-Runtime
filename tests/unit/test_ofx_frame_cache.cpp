#include <catch2/catch_all.hpp>
#include <vector>

#include "plugins/ofx/ofx_frame_cache.hpp"

using namespace corridorkey;
using namespace corridorkey::ofx;

namespace {

// Fill an ImageBuffer with a deterministic gradient so different entries are
// distinguishable by content. Returns the buffer by value.
ImageBuffer make_buffer(int width, int height, int channels, float seed) {
    ImageBuffer buf(width, height, channels);
    Image view = buf.view();
    for (std::size_t i = 0; i < view.data.size(); ++i) {
        view.data[i] = seed + static_cast<float>(i) * 0.0001f;
    }
    return buf;
}

SharedCacheKey make_key(std::uint64_t distinct) {
    SharedCacheKey key;
    key.frame_signature = 1000 + distinct;
    key.inference_hash = 2000 + distinct;
    key.model_path_hash = 3000;
    key.screen_color = 0;
    return key;
}

}  // namespace

TEST_CASE("shared frame cache records hit and miss counters", "[unit][ofx][cache]") {
    SharedFrameCache cache;
    const auto key = make_key(1);
    auto alpha = make_buffer(4, 4, 1, 0.1f);
    auto foreground = make_buffer(4, 4, 3, 0.2f);

    ImageBuffer out_alpha;
    ImageBuffer out_foreground;

    REQUIRE_FALSE(cache.try_retrieve(key, out_alpha, out_foreground));
    REQUIRE(cache.stats().misses == 1);
    REQUIRE(cache.stats().hits == 0);

    cache.store(key, alpha.view(), foreground.view());
    REQUIRE(cache.stats().stores == 1);

    REQUIRE(cache.try_retrieve(key, out_alpha, out_foreground));
    REQUIRE(cache.stats().hits == 1);
    REQUIRE(cache.stats().misses == 1);
}

TEST_CASE("shared frame cache evicts least-recently-accessed entries under budget pressure",
          "[unit][ofx][cache]") {
    // Small budget forces eviction after storing a few entries of known size.
    // A 4x4x1 buffer with float32 data is 16 * 4 = 64 bytes, so each store
    // contributes ~64 (alpha) + 192 (rgb foreground) = 256 bytes plus padding.
    // Using a 1 KiB budget makes the eviction boundary predictable without
    // allocating large buffers in the test.
    SharedFrameCache cache(1024);

    auto alpha = make_buffer(4, 4, 1, 0.1f);
    auto foreground = make_buffer(4, 4, 3, 0.2f);

    const auto key_a = make_key(1);
    const auto key_b = make_key(2);
    const auto key_c = make_key(3);
    const auto key_d = make_key(4);
    const auto key_e = make_key(5);

    cache.store(key_a, alpha.view(), foreground.view());
    cache.store(key_b, alpha.view(), foreground.view());

    // Touch key_a so it becomes most-recent. key_b is now the LRU victim.
    ImageBuffer out_alpha;
    ImageBuffer out_foreground;
    REQUIRE(cache.try_retrieve(key_a, out_alpha, out_foreground));

    cache.store(key_c, alpha.view(), foreground.view());
    cache.store(key_d, alpha.view(), foreground.view());
    cache.store(key_e, alpha.view(), foreground.view());

    const auto stats = cache.stats();
    REQUIRE(stats.bytes <= 1024);
    REQUIRE(stats.evictions >= 1);

    // key_a was recently touched, so it should still be present. key_b was
    // not touched and is the oldest, so it should be the first evicted.
    REQUIRE(cache.try_retrieve(key_a, out_alpha, out_foreground));
    REQUIRE_FALSE(cache.try_retrieve(key_b, out_alpha, out_foreground));
}

TEST_CASE("shared frame cache updates existing entry in place when the key repeats",
          "[unit][ofx][cache]") {
    SharedFrameCache cache;
    const auto key = make_key(7);
    auto alpha_v1 = make_buffer(4, 4, 1, 0.1f);
    auto foreground_v1 = make_buffer(4, 4, 3, 0.2f);
    auto alpha_v2 = make_buffer(4, 4, 1, 0.9f);
    auto foreground_v2 = make_buffer(4, 4, 3, 0.8f);

    cache.store(key, alpha_v1.view(), foreground_v1.view());
    cache.store(key, alpha_v2.view(), foreground_v2.view());

    REQUIRE(cache.stats().entries == 1);
    REQUIRE(cache.stats().stores == 2);

    ImageBuffer out_alpha;
    ImageBuffer out_foreground;
    REQUIRE(cache.try_retrieve(key, out_alpha, out_foreground));
    REQUIRE(out_alpha.view()(0, 0) == Catch::Approx(0.9f));
    REQUIRE(out_foreground.view()(0, 0) == Catch::Approx(0.8f));
}

TEST_CASE("shared frame cache stats report the byte budget so diagnostics can surface it",
          "[unit][ofx][cache]") {
    SharedFrameCache cache(1024);
    const auto stats = cache.stats();
    REQUIRE(stats.byte_budget == 1024);
    REQUIRE(stats.entries == 0);
    REQUIRE(stats.bytes == 0);
}

TEST_CASE("shared frame cache keeps the most recent entry even if it alone exceeds the budget",
          "[unit][ofx][cache]") {
    // Budget of 256 bytes is smaller than a 4x4 RGB+alpha entry. The cache
    // should admit the entry anyway so the immediate repeat still serves a
    // cache hit, while evicting any older entries to keep the invariant that
    // the most recent render is always retrievable.
    SharedFrameCache cache(256);

    auto alpha = make_buffer(4, 4, 1, 0.1f);
    auto foreground = make_buffer(4, 4, 3, 0.2f);
    const auto key = make_key(42);

    cache.store(key, alpha.view(), foreground.view());

    ImageBuffer out_alpha;
    ImageBuffer out_foreground;
    REQUIRE(cache.try_retrieve(key, out_alpha, out_foreground));
}
