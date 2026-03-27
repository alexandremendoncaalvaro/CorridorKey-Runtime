#include "ofx_frame_cache.hpp"

#include <cstring>
#include <optional>

namespace corridorkey::ofx {

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

struct RetrievedEntry {
    Image alpha;
    Image foreground;
};

}  // namespace

std::uint64_t inference_params_hash(const InferenceParams& params) {
    std::uint64_t hash = kFnvOffsetBasis;
    hash = mix_signature(hash, static_cast<float>(params.target_resolution));
    hash = mix_signature(hash, params.despill_strength);
    hash = mix_signature(hash, static_cast<float>(params.spill_method));
    hash = mix_signature(hash, params.auto_despeckle ? 1.0f : 0.0f);
    hash = mix_signature(hash, static_cast<float>(params.despeckle_size));
    hash = mix_signature(hash, params.refiner_scale);
    hash = mix_signature(hash, static_cast<float>(static_cast<std::uint8_t>(params.alpha_hint_policy)));
    hash = mix_signature(hash, params.input_is_linear ? 1.0f : 0.0f);
    hash = mix_signature(hash, static_cast<float>(params.batch_size));
    hash = mix_signature(hash, params.enable_tiling ? 1.0f : 0.0f);
    hash = mix_signature(hash, static_cast<float>(params.tile_padding));
    hash = mix_signature(hash,
                         static_cast<float>(static_cast<std::uint8_t>(params.upscale_method)));
    hash = mix_signature(hash, params.source_passthrough ? 1.0f : 0.0f);
    hash = mix_signature(hash, static_cast<float>(params.sp_erode_px));
    hash = mix_signature(hash, static_cast<float>(params.sp_blur_px));
    hash = mix_signature(hash, static_cast<float>(params.requested_quality_resolution));
    hash = mix_signature(
        hash, static_cast<float>(static_cast<std::uint8_t>(params.quality_fallback_mode)));
    hash = mix_signature(hash, static_cast<float>(static_cast<std::uint8_t>(params.refinement_mode)));
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

bool SharedFrameCache::try_retrieve(const SharedCacheKey& key, ImageBuffer& out_alpha,
                                    ImageBuffer& out_foreground) const {
    std::optional<RetrievedEntry> snapshot;

    {
        std::shared_lock lock(m_mutex);
        for (const auto& entry : m_entries) {
            if (!entry.occupied || entry.key != key) {
                continue;
            }
            const Image a = entry.alpha.const_view();
            const Image f = entry.foreground.const_view();
            if (a.data.empty() || f.data.empty()) {
                continue;
            }
            snapshot = RetrievedEntry{a, f};
            break;
        }
    }

    if (!snapshot.has_value()) {
        return false;
    }

    auto alpha = deep_copy_buffer(snapshot->alpha);
    auto foreground = deep_copy_buffer(snapshot->foreground);
    if (!alpha.has_value() || !foreground.has_value()) {
        return false;
    }
    out_alpha = std::move(*alpha);
    out_foreground = std::move(*foreground);
    return true;
}

void SharedFrameCache::store(const SharedCacheKey& key, const Image& alpha,
                             const Image& foreground) {
    auto alpha_copy = deep_copy_buffer(alpha);
    auto fg_copy = deep_copy_buffer(foreground);
    if (!alpha_copy.has_value() || !fg_copy.has_value()) {
        return;
    }

    std::unique_lock lock(m_mutex);

    for (auto& entry : m_entries) {
        if (entry.occupied && entry.key == key) {
            entry.alpha = std::move(*alpha_copy);
            entry.foreground = std::move(*fg_copy);
            return;
        }
    }

    auto& slot = m_entries[m_next_slot];
    slot.key = key;
    slot.alpha = std::move(*alpha_copy);
    slot.foreground = std::move(*fg_copy);
    slot.occupied = true;
    m_next_slot = (m_next_slot + 1) % kMaxEntries;
}

void SharedFrameCache::clear() {
    std::unique_lock lock(m_mutex);
    for (auto& entry : m_entries) {
        entry.key = {};
        entry.alpha = {};
        entry.foreground = {};
        entry.occupied = false;
    }
    m_next_slot = 0;
}

}  // namespace corridorkey::ofx
