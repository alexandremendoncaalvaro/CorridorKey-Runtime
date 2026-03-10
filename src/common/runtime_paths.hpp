#pragma once

#include <corridorkey/types.hpp>
#include <cstdlib>
#include <filesystem>
#include <string_view>

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

}  // namespace detail

inline std::filesystem::path default_cache_root() {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        return std::filesystem::path(home) / "Library" / "Caches" / "CorridorKey";
    }
#endif
    return std::filesystem::temp_directory_path() / "corridorkey-cache";
}

inline std::filesystem::path optimized_model_cache_path(const std::filesystem::path& model_path,
                                                        Backend backend) {
    auto cache_root = default_cache_root() / "optimized_models";
    auto stem = model_path.stem().string();
    auto extension = model_path.extension().string();
    if (extension.empty()) {
        extension = ".onnx";
    }

    auto cache_name = stem + "_" + detail::backend_token(backend) + "_" +
                      detail::cache_key_for_model(model_path, backend) + extension;
    return cache_root / cache_name;
}

}  // namespace corridorkey::common
