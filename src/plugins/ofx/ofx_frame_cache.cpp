#include "ofx_frame_cache.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <optional>

namespace corridorkey::ofx {

// NOLINTBEGIN(bugprone-easily-swappable-parameters,readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,modernize-use-ranges)
//
// ofx_frame_cache.cpp tidy-suppression rationale.
//
// This file is a small FNV-1a hashing and LRU eviction layer. The
// adjacent-parameter pairs (hash + value, alpha + foreground) describe
// the cache primitive's contract and have no safer ordering. The single-
// character `c` / `a` / `f` locals mirror the FNV byte-loop and the
// (alpha, foreground) buffer-pair convention used throughout the OFX
// plugin. The 32-entry vector reserve hint reflects the typical scrub-
// burst size and does not warrant a named constant. std::min_element
// with the access-tick comparator has no equivalent ranges form that
// preserves the iterator-erase pattern used here for LRU eviction.
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t mix_signature(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(bits));
    std::memcpy(&bits, &value, sizeof(bits));
    hash ^= static_cast<std::uint64_t>(bits);
    return hash * kFnvPrime;
}

std::optional<ImageBuffer> deep_copy_buffer(const Image& src) {
    if (src.width <= 0 || src.height <= 0 || src.channels <= 0 || src.data.empty()) {
        return std::nullopt;
    }
    ImageBuffer buf(src.width, src.height, src.channels);
    if (buf.view().data.empty()) {
        return std::nullopt;
    }
    std::memcpy(buf.view().data.data(), src.data.data(), src.data.size_bytes());
    return buf;
}

std::size_t buffer_bytes(const ImageBuffer& buffer) {
    return buffer.const_view().data.size_bytes();
}

}  // namespace

std::uint64_t inference_params_hash(const InferenceParams& params) {
    std::uint64_t hash = kFnvOffsetBasis;
    hash = mix_signature(hash, static_cast<float>(params.target_resolution));
    hash = mix_signature(hash, params.despill_strength);
    hash = mix_signature(hash, static_cast<float>(params.spill_method));
    hash = mix_signature(hash, params.auto_despeckle ? 1.0F : 0.0F);
    hash = mix_signature(hash, static_cast<float>(params.despeckle_size));
    hash = mix_signature(hash, params.refiner_scale);
    hash = mix_signature(hash,
                         static_cast<float>(static_cast<std::uint8_t>(params.alpha_hint_policy)));
    hash = mix_signature(hash, params.input_is_linear ? 1.0F : 0.0F);
    hash = mix_signature(hash, static_cast<float>(params.batch_size));
    hash = mix_signature(hash, params.enable_tiling ? 1.0F : 0.0F);
    hash = mix_signature(hash, static_cast<float>(params.tile_padding));
    hash =
        mix_signature(hash, static_cast<float>(static_cast<std::uint8_t>(params.upscale_method)));
    hash = mix_signature(hash, params.source_passthrough ? 1.0F : 0.0F);
    hash = mix_signature(hash, static_cast<float>(params.sp_erode_px));
    hash = mix_signature(hash, static_cast<float>(params.sp_blur_px));
    hash = mix_signature(hash, params.output_alpha_only ? 1.0F : 0.0F);
    hash = mix_signature(hash, static_cast<float>(params.requested_quality_resolution));
    hash = mix_signature(
        hash, static_cast<float>(static_cast<std::uint8_t>(params.quality_fallback_mode)));
    hash =
        mix_signature(hash, static_cast<float>(static_cast<std::uint8_t>(params.refinement_mode)));
    hash = mix_signature(hash, static_cast<float>(params.coarse_resolution_override));
    return hash;
}

std::uint64_t path_hash(const std::filesystem::path& path) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (const char c : path.string()) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t frame_signature(const Image& rgb, const Image& hint) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (float value : rgb.data) {
        hash = mix_signature(hash, value);
    }
    if (hint.width == rgb.width && hint.height == rgb.height) {
        for (float value : hint.data) {
            hash = mix_signature(hash, value);
        }
    }
    hash = mix_signature(hash, static_cast<float>(rgb.width));
    hash = mix_signature(hash, static_cast<float>(rgb.height));
    hash = mix_signature(hash, static_cast<float>(rgb.channels));
    return hash;
}

SharedFrameCache::SharedFrameCache(std::size_t byte_budget) : m_byte_budget(byte_budget) {
    // Reserve a reasonable capacity to avoid frequent vector growth on the
    // first dozens of scrub-driven stores. The eventual size is dictated by
    // the byte budget; the hint here only shapes allocator behavior.
    m_entries.reserve(32);
}

bool SharedFrameCache::try_retrieve(const SharedCacheKey& key, ImageBuffer& out_alpha,
                                    ImageBuffer& out_foreground,
                                    std::vector<StageTiming>* out_stage_timings) {
    std::optional<Image> alpha_view;
    std::optional<Image> foreground_view;
    std::vector<StageTiming> stage_timings_snapshot;

    {
        const std::scoped_lock lock(m_mutex);
        for (auto& entry : m_entries) {
            if (entry.key != key) {
                continue;
            }
            const Image a = entry.alpha.const_view();
            const Image f = entry.foreground.const_view();
            if (a.data.empty() || f.data.empty()) {
                continue;
            }
            alpha_view = a;
            foreground_view = f;
            stage_timings_snapshot = entry.stage_timings;
            entry.last_access_ticks = ++m_access_counter;
            break;
        }
    }

    if (!alpha_view.has_value() || !foreground_view.has_value()) {
        m_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    auto alpha = deep_copy_buffer(*alpha_view);
    auto foreground = deep_copy_buffer(*foreground_view);
    if (!alpha.has_value() || !foreground.has_value()) {
        // Treated as miss because the caller cannot observe the cached result.
        m_misses.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    out_alpha = std::move(*alpha);
    out_foreground = std::move(*foreground);
    if (out_stage_timings != nullptr) {
        *out_stage_timings = std::move(stage_timings_snapshot);
    }
    m_hits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void SharedFrameCache::evict_until_fits(std::size_t incoming_bytes) {
    // No-op when the incoming entry alone exceeds the budget: admit it and
    // evict everything else. The intent is to always keep at least the most
    // recent render in cache, so the immediate repeat of a heavy render still
    // serves from cache even though we're technically over budget by the size
    // of that single entry.
    if (incoming_bytes >= m_byte_budget) {
        for (auto& entry : m_entries) {
            m_current_bytes -= std::min(entry.byte_size, m_current_bytes);
            entry = CacheEntry{};
        }
        m_entries.clear();
        const auto removed = m_evictions.load(std::memory_order_relaxed);
        m_evictions.store(removed + 0, std::memory_order_relaxed);
        return;
    }

    while (m_current_bytes + incoming_bytes > m_byte_budget && !m_entries.empty()) {
        auto oldest = std::min_element(m_entries.begin(), m_entries.end(),
                                       [](const CacheEntry& a, const CacheEntry& b) {
                                           return a.last_access_ticks < b.last_access_ticks;
                                       });
        m_current_bytes -= std::min(oldest->byte_size, m_current_bytes);
        m_entries.erase(oldest);
        m_evictions.fetch_add(1, std::memory_order_relaxed);
    }
}

void SharedFrameCache::store(const SharedCacheKey& key, const Image& alpha, const Image& foreground,
                             std::vector<StageTiming> stage_timings) {
    auto alpha_copy = deep_copy_buffer(alpha);
    auto fg_copy = deep_copy_buffer(foreground);
    if (!alpha_copy.has_value() || !fg_copy.has_value()) {
        return;
    }
    const std::size_t incoming_bytes = buffer_bytes(*alpha_copy) + buffer_bytes(*fg_copy);

    const std::scoped_lock lock(m_mutex);

    // Update-in-place when the same key is re-stored. Common when the plugin
    // re-renders a frame it previously cached but with a refreshed timings
    // vector (the caller's snapshot is newer).
    for (auto& entry : m_entries) {
        if (entry.key == key) {
            m_current_bytes -= std::min(entry.byte_size, m_current_bytes);
            entry.alpha = std::move(*alpha_copy);
            entry.foreground = std::move(*fg_copy);
            entry.stage_timings = std::move(stage_timings);
            entry.last_access_ticks = ++m_access_counter;
            entry.byte_size = incoming_bytes;
            m_current_bytes += incoming_bytes;
            m_stores.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    evict_until_fits(incoming_bytes);

    CacheEntry entry;
    entry.key = key;
    entry.alpha = std::move(*alpha_copy);
    entry.foreground = std::move(*fg_copy);
    entry.stage_timings = std::move(stage_timings);
    entry.last_access_ticks = ++m_access_counter;
    entry.byte_size = incoming_bytes;
    m_current_bytes += incoming_bytes;
    m_entries.push_back(std::move(entry));
    m_stores.fetch_add(1, std::memory_order_relaxed);
}

void SharedFrameCache::clear() {
    const std::scoped_lock lock(m_mutex);
    m_entries.clear();
    m_current_bytes = 0;
    m_access_counter = 0;
}

SharedFrameCacheStats SharedFrameCache::stats() const {
    SharedFrameCacheStats result;
    result.hits = m_hits.load(std::memory_order_relaxed);
    result.misses = m_misses.load(std::memory_order_relaxed);
    result.stores = m_stores.load(std::memory_order_relaxed);
    result.evictions = m_evictions.load(std::memory_order_relaxed);
    {
        const std::scoped_lock lock(m_mutex);
        result.entries = m_entries.size();
        result.bytes = m_current_bytes;
        result.byte_budget = m_byte_budget;
    }
    return result;
}

}  // namespace corridorkey::ofx
// NOLINTEND(bugprone-easily-swappable-parameters,readability-identifier-length,cppcoreguidelines-avoid-magic-numbers,modernize-use-ranges)
