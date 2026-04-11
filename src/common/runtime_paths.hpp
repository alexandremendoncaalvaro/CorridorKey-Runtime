#pragma once

#include <algorithm>
#include <array>
#include <corridorkey/types.hpp>
#include <corridorkey/version.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace corridorkey::common {

enum class ModelArtifactStatus : std::uint8_t { Missing, LfsPlaceholder, Invalid, Usable };

struct ModelArtifactInspection {
    ModelArtifactStatus status = ModelArtifactStatus::Missing;
    bool found = false;
    bool usable = false;
    std::uintmax_t size_bytes = 0;
    std::string detail = "";
};

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
        case Backend::MLX:
            return "mlx";
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

inline std::string portable_model_fingerprint(const std::filesystem::path& model_path,
                                              Backend backend) {
    std::error_code error;
    std::uint64_t file_size = 0;
    if (std::filesystem::exists(model_path, error)) {
        file_size = std::filesystem::file_size(model_path, error);
    }

    auto timestamp = std::filesystem::last_write_time(model_path, error);
    long long ticks = error ? 0LL : static_cast<long long>(timestamp.time_since_epoch().count());

    // Include the application version so any binary update (which may change EP configuration
    // such as optimization profiles) automatically invalidates stale cached engines.
    auto key = model_path.filename().string() + "|" + std::to_string(file_size) + "|" +
               std::to_string(ticks) + "|" + backend_token(backend) + "|" +
               std::string(CORRIDORKEY_VERSION_STRING);
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

inline std::string model_artifact_status_to_string(ModelArtifactStatus status) {
    switch (status) {
        case ModelArtifactStatus::Missing:
            return "missing";
        case ModelArtifactStatus::LfsPlaceholder:
            return "lfs_placeholder";
        case ModelArtifactStatus::Invalid:
            return "invalid";
        case ModelArtifactStatus::Usable:
            return "usable";
    }
    return "invalid";
}

inline bool looks_like_git_lfs_pointer(std::string_view contents) {
    if (contents.find("version https://git-lfs.github.com/spec/v1") != 0) {
        return false;
    }

    return contents.find("oid sha256:") != std::string_view::npos &&
           contents.find("size ") != std::string_view::npos;
}

inline ModelArtifactInspection inspect_model_artifact(const std::filesystem::path& model_path) {
    ModelArtifactInspection inspection;

    std::error_code error;
    inspection.found = std::filesystem::exists(model_path, error) && !error;
    if (!inspection.found) {
        inspection.status = ModelArtifactStatus::Missing;
        inspection.detail = "Artifact not found: " + model_path.string();
        return inspection;
    }

    inspection.size_bytes = std::filesystem::file_size(model_path, error);
    if (error) {
        inspection.status = ModelArtifactStatus::Invalid;
        inspection.detail = "Failed to read artifact size: " + model_path.string();
        return inspection;
    }

    if (inspection.size_bytes == 0) {
        inspection.status = ModelArtifactStatus::Invalid;
        inspection.detail = "Artifact is empty: " + model_path.string();
        return inspection;
    }

    std::ifstream file(model_path, std::ios::binary);
    if (!file.is_open()) {
        inspection.status = ModelArtifactStatus::Invalid;
        inspection.detail = "Failed to open artifact: " + model_path.string();
        return inspection;
    }

    constexpr std::size_t kPointerProbeBytes = 512;
    std::string header(static_cast<std::size_t>(
                           std::min<std::uintmax_t>(inspection.size_bytes, kPointerProbeBytes)),
                       '\0');
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(file.gcount()));
    if (looks_like_git_lfs_pointer(header)) {
        inspection.status = ModelArtifactStatus::LfsPlaceholder;
        inspection.detail =
            "Artifact is a Git LFS pointer placeholder. Hydrate it before running the runtime: " +
            model_path.string();
        return inspection;
    }

    inspection.status = ModelArtifactStatus::Usable;
    inspection.usable = true;
    return inspection;
}

inline std::optional<std::string> environment_variable_copy(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return std::nullopt;
    }

    std::string copy(value, length > 0 ? length - 1 : 0);
    std::free(value);
    return copy;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }

    return std::string(value);
#endif
}

inline std::optional<std::filesystem::path> current_executable_path() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
    }
#elif defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#else
    std::array<char, PATH_MAX> buffer{};
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length > 0) {
        return std::filesystem::weakly_canonical(
            std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length))));
    }
#endif
    return std::nullopt;
}

inline std::filesystem::path default_models_root() {
    if (auto override_path = environment_variable_copy("CORRIDORKEY_MODELS_DIR");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }

    if (auto executable_path = current_executable_path(); executable_path.has_value()) {
        auto executable_dir = executable_path->parent_path();
        std::error_code error;

        auto packaged_candidate = executable_dir.parent_path() / "models";
        if (std::filesystem::exists(packaged_candidate, error) && !error) {
            return packaged_candidate;
        }

        auto sibling_candidate = executable_dir / "models";
        if (std::filesystem::exists(sibling_candidate, error) && !error) {
            return sibling_candidate;
        }
    }

    return "models";
}

inline std::optional<std::filesystem::path> cache_root_override() {
    auto override_path = environment_variable_copy("CORRIDORKEY_CACHE_DIR");
    if (!override_path.has_value()) {
        return std::nullopt;
    }

    return std::filesystem::path(*override_path);
}

inline std::filesystem::path configured_cache_root() {
    if (auto override_path = cache_root_override(); override_path.has_value()) {
        return *override_path;
    }

#if defined(__APPLE__)
    if (auto home = environment_variable_copy("HOME"); home.has_value()) {
        return std::filesystem::path(*home) / "Library" / "Caches" / "CorridorKey";
    }
#elif defined(_WIN32)
    if (auto local_app_data = environment_variable_copy("LOCALAPPDATA");
        local_app_data.has_value()) {
        return std::filesystem::path(*local_app_data) / "CorridorKey" / "Cache";
    }
#endif
    return std::filesystem::temp_directory_path() / "corridorkey-cache";
}

inline std::filesystem::path fallback_cache_root() {
    return std::filesystem::temp_directory_path() / "corridorkey-cache";
}

inline std::filesystem::path default_logs_root() {
#if defined(__APPLE__)
    if (auto home = environment_variable_copy("HOME"); home.has_value()) {
        return std::filesystem::path(*home) / "Library" / "Logs" / "CorridorKey";
    }
#elif defined(_WIN32)
    if (auto local_app_data = environment_variable_copy("LOCALAPPDATA");
        local_app_data.has_value()) {
        return std::filesystem::path(*local_app_data) / "CorridorKey" / "Logs";
    }
#endif
    return std::filesystem::temp_directory_path() / "corridorkey-logs";
}

inline std::vector<std::filesystem::path> cache_root_candidates() {
    std::vector<std::filesystem::path> candidates;
    if (auto override_path = cache_root_override(); override_path.has_value()) {
        detail::append_unique_path(candidates, *override_path);
    }

#if defined(__APPLE__)
    if (auto home = environment_variable_copy("HOME"); home.has_value()) {
        detail::append_unique_path(
            candidates, std::filesystem::path(*home) / "Library" / "Caches" / "CorridorKey");
    }
#elif defined(_WIN32)
    if (auto local_app_data = environment_variable_copy("LOCALAPPDATA");
        local_app_data.has_value()) {
        detail::append_unique_path(
            candidates, std::filesystem::path(*local_app_data) / "CorridorKey" / "Cache");
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

inline std::filesystem::path ofx_runtime_root() {
    if (auto override_path = environment_variable_copy("CORRIDORKEY_OFX_RUNTIME_ROOT");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }
    return default_cache_root() / "ofx_runtime" / ("v" + std::string(CORRIDORKEY_VERSION_STRING));
}

inline std::filesystem::path ofx_runtime_shared_frames_root() {
    return ofx_runtime_root() / "frames";
}

inline std::filesystem::path ofx_runtime_server_log_path() {
    if (auto override_path = environment_variable_copy("CORRIDORKEY_OFX_RUNTIME_LOG");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }
    return default_logs_root() /
           ("ofx_runtime_server_v" + std::string(CORRIDORKEY_VERSION_STRING) + ".log");
}

inline std::uint16_t default_ofx_runtime_port() {
    if (auto override_port = environment_variable_copy("CORRIDORKEY_OFX_RUNTIME_PORT");
        override_port.has_value()) {
        return static_cast<std::uint16_t>(std::stoi(*override_port));
    }

    auto cache_root = ofx_runtime_root().string();
    constexpr std::uint16_t kBasePort = 43000;
    constexpr std::uint16_t kPortSpan = 1000;
    return static_cast<std::uint16_t>(kBasePort + (detail::fnv1a_64(cache_root) % kPortSpan));
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

inline std::optional<std::filesystem::path> coreml_model_cache_root() {
    auto cache_root = selected_cache_root();
    if (!cache_root.has_value()) {
        return std::nullopt;
    }

    return *cache_root / "coreml_ep";
}

inline std::optional<std::filesystem::path> tensorrt_rtx_runtime_cache_root() {
    auto cache_root = selected_cache_root();
    if (!cache_root.has_value()) {
        return std::nullopt;
    }

    return *cache_root / "tensorrt_rtx";
}

inline std::optional<std::filesystem::path> tensorrt_rtx_runtime_cache_path(
    const std::filesystem::path& model_path) {
    auto cache_root = tensorrt_rtx_runtime_cache_root();
    if (!cache_root.has_value()) {
        return std::nullopt;
    }

    auto stem = model_path.stem().string();
    auto cache_name =
        stem + "_" + detail::portable_model_fingerprint(model_path, Backend::TensorRT);
    return *cache_root / cache_name;
}

inline std::filesystem::path tensorrt_rtx_compiled_context_model_path(
    const std::filesystem::path& model_path) {
    return model_path.parent_path() / (model_path.stem().string() + "_ctx.onnx");
}

inline std::optional<std::filesystem::path> existing_tensorrt_rtx_compiled_context_model_path(
    const std::filesystem::path& model_path) {
    auto compiled_path = tensorrt_rtx_compiled_context_model_path(model_path);
    std::error_code error;
    if (std::filesystem::exists(compiled_path, error) && !error) {
        return compiled_path;
    }

    return std::nullopt;
}

}  // namespace corridorkey::common
