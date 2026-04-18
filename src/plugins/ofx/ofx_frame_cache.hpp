#pragma once

#include <atomic>
#include <chrono>
#include <corridorkey/types.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace corridorkey::ofx {

struct SharedCacheKey {
    std::uint64_t frame_signature = 0;
    std::uint64_t inference_hash = 0;
    std::uint64_t model_path_hash = 0;
    int screen_color = 0;

    bool operator==(const SharedCacheKey&) const = default;
};

std::uint64_t inference_params_hash(const InferenceParams& params);
std::uint64_t path_hash(const std::filesystem::path& path);
std::uint64_t frame_signature(const Image& rgb, const Image& hint);

// Aggregated counters exposed for diagnostics and tests. Written by the cache
// under its own lock and read via snapshot. The running hit rate over the
// DaVinci Resolve scrub workflow is the single most useful post-slice 0.7.5-3
// signal for confirming the cache does its job.
struct SharedFrameCacheStats {
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t stores = 0;
    std::uint64_t evictions = 0;
    std::size_t entries = 0;
    std::size_t bytes = 0;
    std::size_t byte_budget = 0;
};

// Explicit lifecycle -- created in on_load(), destroyed in kOfxActionUnload.
// Not a singleton: ownership is managed by the caller via unique_ptr.
class SharedFrameCache {
   public:
    // Default byte budget: 512 MiB. Picked to keep a meaningful LRU window even
    // at 4K RGBA32F (~32 MB alpha + ~95 MB foreground per entry, so ~4 entries
    // at 4K, ~8 at UHD-half, ~16-32 at 1080p) without contributing materially
    // to peak RSS alongside DaVinci Resolve on 16 GB Apple Silicon.
    // The byte budget is the authoritative admission gate; the entry count
    // follows.
    static constexpr std::size_t kDefaultByteBudget = 512ULL * 1024ULL * 1024ULL;

    SharedFrameCache() : SharedFrameCache(kDefaultByteBudget) {}
    explicit SharedFrameCache(std::size_t byte_budget);

    SharedFrameCache(const SharedFrameCache&) = delete;
    SharedFrameCache& operator=(const SharedFrameCache&) = delete;

    bool try_retrieve(const SharedCacheKey& key, ImageBuffer& out_alpha,
                      ImageBuffer& out_foreground,
                      std::vector<StageTiming>* out_stage_timings = nullptr);

    void store(const SharedCacheKey& key, const Image& alpha, const Image& foreground,
               std::vector<StageTiming> stage_timings = {});

    void clear();

    [[nodiscard]] SharedFrameCacheStats stats() const;

   private:
    struct CacheEntry {
        SharedCacheKey key = {};
        ImageBuffer alpha = {};
        ImageBuffer foreground = {};
        std::vector<StageTiming> stage_timings = {};
        std::uint64_t last_access_ticks = 0;
        std::size_t byte_size = 0;
    };

    // Caller must hold m_mutex exclusively. Evicts least-recently-accessed
    // entries until current_bytes + incoming_bytes <= byte_budget.
    void evict_until_fits(std::size_t incoming_bytes);

    mutable std::mutex m_mutex;
    std::vector<CacheEntry> m_entries = {};
    std::size_t m_byte_budget = 0;
    std::size_t m_current_bytes = 0;
    std::uint64_t m_access_counter = 0;

    // Counters are read outside m_mutex via snapshot in stats(); atomics avoid
    // a second lock on the read path.
    std::atomic<std::uint64_t> m_hits{0};
    std::atomic<std::uint64_t> m_misses{0};
    std::atomic<std::uint64_t> m_stores{0};
    std::atomic<std::uint64_t> m_evictions{0};
};

}  // namespace corridorkey::ofx
