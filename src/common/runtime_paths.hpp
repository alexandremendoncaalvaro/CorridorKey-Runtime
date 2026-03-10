#pragma once

#include <algorithm>
#include <corridorkey/types.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace corridorkey::common {

namespace detail {

inline std::uint64_t fnv1a_64(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline std::string backend_token(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        default:
            return "auto";
    }
}

inline std::string cache_key_for_model(const std::filesystem::path& model_path, Backend backend) {
    std::error_code error;
    auto canonical_path = std::filesystem::weakly_canonical(model_path, error);
    if (error) {
        canonical_path = model_path;
    }

    std::uint64_t file_size = 0;
    if (std::filesystem::exists(model_path, error)) {
        file_size = std::filesystem::file_size(model_path, error);
    }

    auto timestamp = std::filesystem::last_write_time(model_path, error);
    long long ticks = error ? 0LL : static_cast<long long>(timestamp.time_since_epoch().count());

    auto key = canonical_path.string() + "|" + std::to_string(file_size) + "|" +
               std::to_string(ticks) + "|" + backend_token(backend);
    return std::to_string(fnv1a_64(key));
}

inline void append_unique_path(std::vector<std::filesystem::path>& paths,
                               const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return;
    }

    auto normalized = candidate.lexically_normal();
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
}

}  // namespace detail

inline std::optional<std::filesystem::path> cache_root_override() {
    const char* override_path = std::getenv("CORRIDORKEY_CACHE_DIR");
    if (override_path == nullptr || *override_path == '\0') {
        return std::nullopt;
    }

    return std::filesystem::path(override_path);
}

inline std::filesystem::path configured_cache_root() {
    if (auto override_path = cache_root_override(); override_path.has_value()) {
        return *override_path;
    }

#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        return std::filesystem::path(home) / "Library" / "Caches" / "CorridorKey";
    }
#endif
    return std::filesystem::temp_directory_path() / "corridorkey-cache";
}

inline std::filesystem::path fallback_cache_root() {
    return std::filesystem::temp_directory_path() / "corridorkey-cache";
}

inline std::vector<std::filesystem::path> cache_root_candidates() {
    std::vector<std::filesystem::path> candidates;
    if (auto override_path = cache_root_override(); override_path.has_value()) {
        detail::append_unique_path(candidates, *override_path);
    }

#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        detail::append_unique_path(
            candidates, std::filesystem::path(home) / "Library" / "Caches" / "CorridorKey");
    }
#endif
    detail::append_unique_path(candidates, fallback_cache_root());
    return candidates;
}

inline bool is_cache_root_writable(const std::filesystem::path& cache_root) {
    if (cache_root.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(cache_root, error);
    if (error) {
        return false;
    }

    auto probe = cache_root / (".corridorkey-write-probe-" +
                               std::to_string(detail::fnv1a_64(cache_root.string())));
    std::ofstream stream(probe);
    if (!stream.good()) {
        return false;
    }

    stream << "ok";
    stream.close();
    std::filesystem::remove(probe, error);
    return true;
}

inline std::optional<std::filesystem::path> selected_cache_root() {
    for (const auto& candidate : cache_root_candidates()) {
        if (is_cache_root_writable(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

inline std::filesystem::path default_cache_root() {
    if (auto selected = selected_cache_root(); selected.has_value()) {
        return *selected;
    }

    return configured_cache_root();
}

inline std::optional<std::filesystem::path> optimized_model_cache_path(
    const std::filesystem::path& model_path, Backend backend) {
    auto cache_root = selected_cache_root();
    if (!cache_root.has_value()) {
        return std::nullopt;
    }

    auto optimized_models_dir = *cache_root / "optimized_models";
    auto stem = model_path.stem().string();
    auto extension = model_path.extension().string();
    if (extension.empty()) {
        extension = ".onnx";
    }

    auto cache_name = stem + "_" + detail::backend_token(backend) + "_" +
                      detail::cache_key_for_model(model_path, backend) + extension;
    return optimized_models_dir / cache_name;
}

}  // namespace corridorkey::common
