#pragma once

#include <corridorkey/types.hpp>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>

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

// Explicit lifecycle -- created in on_load(), destroyed in kOfxActionUnload.
// Not a singleton: ownership is managed by the caller via unique_ptr.
class SharedFrameCache {
   public:
    SharedFrameCache() = default;

    SharedFrameCache(const SharedFrameCache&) = delete;
    SharedFrameCache& operator=(const SharedFrameCache&) = delete;

    bool try_retrieve(const SharedCacheKey& key, ImageBuffer& out_alpha,
                      ImageBuffer& out_foreground) const;

    void store(const SharedCacheKey& key, const Image& alpha, const Image& foreground);

    void clear();

   private:
    struct CacheEntry {
        SharedCacheKey key = {};
        ImageBuffer alpha = {};
        ImageBuffer foreground = {};
        bool occupied = false;
    };

    static constexpr std::size_t kMaxEntries = 4;

    mutable std::shared_mutex m_mutex;
    CacheEntry m_entries[kMaxEntries] = {};
    std::size_t m_next_slot = 0;
};

}  // namespace corridorkey::ofx
