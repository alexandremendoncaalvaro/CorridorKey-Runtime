#include "runtime_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <corridorkey/engine.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "../core/inference_session.hpp"
#include "../core/mlx_probe.hpp"
#include "../core/windows_rtx_probe.hpp"
#include "../frame_io/video_io.hpp"
#include "common/runtime_paths.hpp"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/wait.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace corridorkey::app {

namespace {

struct CommandResult {
    int exit_code = -1;
    std::string output = "";
};

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool is_metadata_sidecar(const std::filesystem::path& path) {
    auto filename = path.filename().string();
    return filename == ".DS_Store" || filename.rfind("._", 0) == 0;
}

std::string shell_escape(const std::filesystem::path& path) {
    std::string value = path.string();
    std::string escaped = "'";
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

CommandResult run_command_capture(const std::string& command) {
    CommandResult result;
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr) {
        result.output = "failed to launch command";
        return result;
    }

    std::array<char, 256> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }

#if defined(_WIN32)
    result.exit_code = _pclose(pipe);
#else
    int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
    result.output = trim_copy(result.output);
    return result;
}

std::filesystem::path current_executable_path() {
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
    return {};
}

std::optional<std::filesystem::path> find_runtime_library(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();
#if defined(__APPLE__)
        if (filename.rfind("libonnxruntime", 0) == 0 && entry.path().extension() == ".dylib") {
            return entry.path();
        }
#elif defined(_WIN32)
        if (filename == "onnxruntime.dll") {
            return entry.path();
        }
#else
        if (filename.rfind("libonnxruntime", 0) == 0 && filename.find(".so") != std::string::npos) {
            return entry.path();
        }
#endif
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> find_exact_library(const std::filesystem::path& directory,
                                                        const std::string& filename) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return std::nullopt;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        if (entry.path().filename() == filename) {
            return entry.path();
        }
    }

    return std::nullopt;
}

std::vector<std::filesystem::path> find_libraries_with_prefix(
    const std::filesystem::path& directory, std::string_view prefix) {
    std::vector<std::filesystem::path> matches;
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return matches;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }

        auto filename = entry.path().filename().string();
        if (std::string_view(filename).starts_with(prefix)) {
            matches.push_back(entry.path());
        }
    }

    return matches;
}

void append_unique_paths(std::vector<std::filesystem::path>& destination,
                         const std::vector<std::filesystem::path>& source) {
    for (const auto& path : source) {
        if (std::find(destination.begin(), destination.end(), path) == destination.end()) {
            destination.push_back(path);
        }
    }
}

std::vector<std::filesystem::path> find_libraries_with_prefixes(
    const std::filesystem::path& directory, std::initializer_list<std::string_view> prefixes) {
    std::vector<std::filesystem::path> matches;
    for (auto prefix : prefixes) {
        append_unique_paths(matches, find_libraries_with_prefix(directory, prefix));
    }
    return matches;
}

std::vector<std::string> dependency_references(const std::filesystem::path& executable_path) {
    std::vector<std::string> references;
#if defined(__APPLE__)
    auto command = std::string("/usr/bin/otool -L ") + shell_escape(executable_path) + " 2>&1";
    auto output = run_command_capture(command);
    std::stringstream stream(output.output);
    std::string line;
    bool first_line = true;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (line.empty()) {
            continue;
        }
        if (first_line) {
            first_line = false;
            continue;
        }
        auto marker = line.find(" (");
        if (marker != std::string::npos) {
            line = line.substr(0, marker);
        }
        references.push_back(line);
    }
#else
    (void)executable_path;
#endif
    return references;
}

bool paths_equivalent(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    return std::filesystem::exists(left, error) && std::filesystem::exists(right, error) &&
           std::filesystem::equivalent(left, right, error) && !error;
}

nlohmann::json inspect_signature(const std::filesystem::path& executable_path) {
    nlohmann::json json;
#if defined(__APPLE__)
    json["applicable"] = true;

    auto describe = run_command_capture("/usr/bin/codesign -dv --verbose=4 " +
                                        shell_escape(executable_path) + " 2>&1");
    auto verify = run_command_capture("/usr/bin/codesign --verify --deep --strict " +
                                      shell_escape(executable_path) + " 2>&1");
    auto gatekeeper = run_command_capture("/usr/sbin/spctl -a -t exec -vv " +
                                          shell_escape(executable_path) + " 2>&1");

    bool signed_binary = describe.exit_code == 0;
    bool verified_binary = verify.exit_code == 0;
    bool accepted_by_gatekeeper = gatekeeper.exit_code == 0;

    std::string source = "";
    std::stringstream stream(gatekeeper.output);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_copy(line);
        if (line.rfind("source=", 0) == 0) {
            source = line.substr(std::string("source=").size());
            break;
        }
    }

    json["signed"] = signed_binary;
    json["verified"] = verified_binary;
    json["gatekeeper_accepted"] = accepted_by_gatekeeper;
    json["notarized"] = source.find("Notarized") != std::string::npos;
    json["source"] = source;
    json["details"] = describe.output;
    json["assessment"] = gatekeeper.output;
#else
    (void)executable_path;
    json["applicable"] = false;
    json["signed"] = false;
    json["verified"] = false;
    json["gatekeeper_accepted"] = false;
    json["notarized"] = false;
#endif
    return json;
}

nlohmann::json inspect_bundle(const std::filesystem::path& models_dir,
                              const std::filesystem::path& executable_path) {
    nlohmann::json json;
    std::filesystem::path executable_dir = executable_path.parent_path();
    std::filesystem::path bundle_root =
        executable_dir.filename() == "bin" ? executable_dir.parent_path() : executable_dir;
    std::filesystem::path expected_models_dir = bundle_root / "models";
    std::filesystem::path readme_path = bundle_root / "README.txt";
    std::filesystem::path smoke_test_path =
#if defined(_WIN32)
        bundle_root / "smoke_test.bat";
#else
        bundle_root / "smoke_test.sh";
#endif

    auto runtime_library = find_runtime_library(executable_dir);
    auto core_library = find_exact_library(executable_dir,
#if defined(_WIN32)
                                           "corridorkey_core.dll"
#else
                                           "libcorridorkey_core.dylib"
#endif
    );
    auto mlx_library = find_exact_library(executable_dir, "libmlx.dylib");
    auto references = dependency_references(executable_path);
    bool core_reference_found =
        std::any_of(references.begin(), references.end(), [](const std::string& reference) {
            return reference.find(
#if defined(_WIN32)
                       "corridorkey_core.dll"
#else
                       "libcorridorkey_core.dylib"
#endif
                       ) != std::string::npos;
        });
    auto core_references = core_library.has_value() ? dependency_references(*core_library)
                                                    : std::vector<std::string>{};
    bool runtime_reference_found = std::any_of(
        core_references.begin(), core_references.end(), [](const std::string& reference) {
            return reference.find("libonnxruntime") != std::string::npos ||
                   reference.find("onnxruntime.dll") != std::string::npos;
        });
    bool mlx_reference_found = std::any_of(
        core_references.begin(), core_references.end(), [](const std::string& reference) {
            return reference.find("libmlx.dylib") != std::string::npos;
        });
    auto mlx_metallib = find_exact_library(executable_dir, "mlx.metallib");
    std::vector<std::filesystem::path> tensorrt_provider_libraries;
    if (auto exact_rtx_provider =
            find_exact_library(executable_dir, "onnxruntime_providers_nv_tensorrt_rtx.dll");
        exact_rtx_provider.has_value()) {
        tensorrt_provider_libraries.push_back(*exact_rtx_provider);
    }
    if (auto legacy_rtx_provider =
            find_exact_library(executable_dir, "onnxruntime_providers_nvtensorrtrtx.dll");
        legacy_rtx_provider.has_value()) {
        append_unique_paths(tensorrt_provider_libraries, {*legacy_rtx_provider});
    }
    append_unique_paths(
        tensorrt_provider_libraries,
        find_libraries_with_prefixes(executable_dir, {"onnxruntime_providers_nv_tensorrt_rtx",
                                                      "onnxruntime_providers_nvtensorrtrtx"}));
    auto vendor_provider_libraries =
        find_libraries_with_prefix(executable_dir, "onnxruntime_providers_");
    auto tensorrt_rtx_core_libraries =
        find_libraries_with_prefixes(executable_dir, {"tensorrt_rtx", "nvinfer"});
    auto tensorrt_rtx_parser_libraries =
        find_libraries_with_prefixes(executable_dir, {"tensorrt_onnxparser_rtx", "nvonnxparser"});
    auto cuda_runtime_libraries =
        find_libraries_with_prefixes(executable_dir, {"cudart64_", "cudart"});
    auto compiled_context_models = nlohmann::json::array();

    nlohmann::json packaged_models = nlohmann::json::array();
    bool packaged_models_present = true;
    for (const auto& model : model_catalog()) {
        bool packaged_for_current_platform =
#if defined(_WIN32)
            model.packaged_for_windows;
#else
            model.packaged_for_macos;
#endif
        if (!packaged_for_current_platform) {
            continue;
        }

        std::filesystem::path model_path = models_dir / model.filename;
        bool found = std::filesystem::exists(model_path);
        packaged_models_present = packaged_models_present && found;

        nlohmann::json entry;
        entry["filename"] = model.filename;
        entry["found"] = found;
        entry["validated_platforms"] = model.validated_platforms;
        entry["validated_hardware_tiers"] = model.validated_hardware_tiers;
        packaged_models.push_back(entry);
    }

    nlohmann::json mlx_bridge_artifacts = nlohmann::json::array();
    bool mlx_bridge_present = false;
    std::error_code directory_error;
    if (std::filesystem::exists(models_dir, directory_error)) {
        for (const auto& entry : std::filesystem::directory_iterator(models_dir, directory_error)) {
            if (directory_error || !entry.is_regular_file() || is_metadata_sidecar(entry.path())) {
                continue;
            }

            if (entry.path().extension() == ".onnx" &&
                entry.path().stem().string().find("_ctx") != std::string::npos) {
                compiled_context_models.push_back(entry.path().filename().string());
                continue;
            }

            if (entry.path().extension() != ".mlxfn") {
                continue;
            }

            mlx_bridge_artifacts.push_back(entry.path().filename().string());
            mlx_bridge_present = true;
        }
    }

    bool packaged_layout_detected =
        executable_dir.filename() == "bin" && std::filesystem::exists(readme_path) &&
        std::filesystem::exists(smoke_test_path) && std::filesystem::exists(expected_models_dir);

    json["root"] = bundle_root.string();
    json["packaged_layout_detected"] = packaged_layout_detected;
    json["readme_present"] = std::filesystem::exists(readme_path);
    json["smoke_test_present"] = std::filesystem::exists(smoke_test_path);
    json["models_dir_exists"] = std::filesystem::exists(models_dir);
    json["models_dir_matches_bundle"] = paths_equivalent(models_dir, expected_models_dir);
    json["runtime_library_found"] = runtime_library.has_value();
    json["runtime_library_path"] = runtime_library.has_value() ? runtime_library->string() : "";
    json["runtime_library_referenced"] = runtime_reference_found;
    json["core_library_found"] = core_library.has_value();
    json["core_library_path"] = core_library.has_value() ? core_library->string() : "";
    json["core_library_referenced"] = core_reference_found;
    json["mlx_library_found"] = mlx_library.has_value();
    json["mlx_library_path"] = mlx_library.has_value() ? mlx_library->string() : "";
    json["mlx_library_referenced"] = mlx_reference_found;
    json["mlx_metallib_found"] = mlx_metallib.has_value();
    json["mlx_metallib_path"] = mlx_metallib.has_value() ? mlx_metallib->string() : "";
    json["mlx_bridge_present"] = mlx_bridge_present;
    json["mlx_bridge_artifacts"] = mlx_bridge_artifacts;
    json["compiled_context_models"] = compiled_context_models;
    json["tensorrt_rtx_provider_libraries"] = nlohmann::json::array();
    for (const auto& path : tensorrt_provider_libraries) {
        json["tensorrt_rtx_provider_libraries"].push_back(path.filename().string());
    }
    json["tensorrt_rtx_provider_found"] = !tensorrt_provider_libraries.empty();
    json["provider_library_count"] = vendor_provider_libraries.size();
    json["windows_rtx_runtime_libraries"] = nlohmann::json::array();
    for (const auto& path : tensorrt_rtx_core_libraries) {
        json["windows_rtx_runtime_libraries"].push_back(path.filename().string());
    }
    json["tensorrt_rtx_parser_libraries"] = nlohmann::json::array();
    for (const auto& path : tensorrt_rtx_parser_libraries) {
        json["tensorrt_rtx_parser_libraries"].push_back(path.filename().string());
    }
    json["cuda_runtime_libraries"] = nlohmann::json::array();
    for (const auto& path : cuda_runtime_libraries) {
        json["cuda_runtime_libraries"].push_back(path.filename().string());
    }
    json["tensorrt_rtx_runtime_found"] = !tensorrt_rtx_core_libraries.empty();
    json["tensorrt_rtx_parser_found"] = !tensorrt_rtx_parser_libraries.empty();
    json["cuda_runtime_found"] = !cuda_runtime_libraries.empty();
    json["dependency_references"] = references;
    json["core_dependency_references"] = core_references;
    json["packaged_models"] = packaged_models;
    json["signature"] = inspect_signature(executable_path);
#if defined(_WIN32)
    json["healthy"] = packaged_layout_detected && runtime_library.has_value() &&
                      core_library.has_value() && packaged_models_present &&
                      !tensorrt_provider_libraries.empty() &&
                      !tensorrt_rtx_core_libraries.empty() &&
                      !tensorrt_rtx_parser_libraries.empty() && !cuda_runtime_libraries.empty();
#else
    json["healthy"] = packaged_layout_detected && runtime_library.has_value() &&
                      runtime_reference_found && core_library.has_value() && core_reference_found &&
                      mlx_library.has_value() && mlx_reference_found && mlx_metallib.has_value() &&
                      mlx_bridge_present && packaged_models_present;
#endif
    return json;
}

nlohmann::json inspect_video_stack() {
    auto encoders = available_video_encoders_for_path("doctor_output.mp4");
    bool portable_h264_available =
        std::any_of(encoders.begin(), encoders.end(), [](const std::string& encoder) {
            return encoder == "h264_videotoolbox" || encoder == "libx264" || encoder == "h264";
        });

    nlohmann::json json;
    json["default_encoder"] = default_video_encoder_for_path("doctor_output.mp4");
    json["supported_encoders"] = encoders;
    json["videotoolbox_available"] = is_videotoolbox_available();
    json["portable_h264_available"] = portable_h264_available;
    json["healthy"] = portable_h264_available;
    return json;
}

nlohmann::json inspect_cache() {
    auto configured_cache_dir = common::configured_cache_root();
    auto selected_cache_dir = common::selected_cache_root();
    auto effective_cache_dir = selected_cache_dir.value_or(configured_cache_dir);
    auto optimized_models_dir = effective_cache_dir / "optimized_models";
    auto coreml_ep_dir = common::coreml_model_cache_root();
    auto tensorrt_rtx_dir = common::tensorrt_rtx_runtime_cache_root();

    nlohmann::json optimized_models = nlohmann::json::array();
    nlohmann::json candidates = nlohmann::json::array();
    for (const auto& candidate : common::cache_root_candidates()) {
        candidates.push_back(candidate.string());
    }

    std::error_code error;
    if (std::filesystem::exists(optimized_models_dir, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(optimized_models_dir, error)) {
            if (error || !entry.is_regular_file()) {
                continue;
            }
            optimized_models.push_back(entry.path().filename().string());
        }
    }

    nlohmann::json json;
    json["path"] = effective_cache_dir.string();
    json["configured_path"] = configured_cache_dir.string();
    json["selected_path"] =
        selected_cache_dir.has_value() ? selected_cache_dir->string() : std::string();
    json["writable"] = selected_cache_dir.has_value();
    json["fallback_in_use"] =
        selected_cache_dir.has_value() && selected_cache_dir.value() != configured_cache_dir;
    json["candidates"] = candidates;
    json["optimized_models_dir"] = optimized_models_dir.string();
    json["optimized_model_count"] = optimized_models.size();
    json["optimized_models"] = optimized_models;
    json["coreml_ep_cache_dir"] =
        coreml_ep_dir.has_value() ? coreml_ep_dir->string() : std::string();
    json["tensorrt_rtx_cache_dir"] =
        tensorrt_rtx_dir.has_value() ? tensorrt_rtx_dir->string() : std::string();
    json["healthy"] = selected_cache_dir.has_value();
    return json;
}

nlohmann::json inspect_coreml_execution_provider(const std::filesystem::path& models_dir) {
#if !defined(__APPLE__)
    (void)models_dir;
#endif
    nlohmann::json json;
    json["applicable"] = false;
    json["available"] = false;
    json["probe_policy"] = "session.disable_cpu_ep_fallback=1";
    json["healthy"] = false;
    json["all_packaged_models_supported"] = false;
    json["models"] = nlohmann::json::array();

#if defined(__APPLE__)
    json["applicable"] = true;

    auto detected_device = auto_detect();
    bool coreml_available = detected_device.backend == Backend::CoreML;
    json["available"] = coreml_available;
    json["detected_device"] = detected_device.name;

    if (!coreml_available) {
        return json;
    }

    DeviceInfo probe_device = detected_device;
    probe_device.backend = Backend::CoreML;

    bool all_packaged_models_supported = true;
    bool any_packaged_model_found = false;

    for (const auto& model : model_catalog()) {
        if (!model.packaged_for_macos || model.artifact_family != "onnx") {
            continue;
        }

        std::filesystem::path model_path = models_dir / model.filename;
        bool found = std::filesystem::exists(model_path);
        any_packaged_model_found = any_packaged_model_found || found;

        nlohmann::json entry;
        entry["filename"] = model.filename;
        entry["found"] = found;
        entry["validated_platforms"] = model.validated_platforms;
        entry["validated_hardware_tiers"] = model.validated_hardware_tiers;
        entry["full_graph_supported"] = false;
        entry["error"] = "";

        if (!found) {
            all_packaged_models_supported = false;
            json["models"].push_back(entry);
            continue;
        }

        auto probe_res = InferenceSession::create(
            model_path, probe_device,
            SessionCreateOptions{.disable_cpu_ep_fallback = true,
                                 .log_severity = ORT_LOGGING_LEVEL_WARNING});
        entry["full_graph_supported"] = probe_res.has_value();
        if (!probe_res) {
            entry["error"] = probe_res.error().message;
            all_packaged_models_supported = false;
        }

        json["models"].push_back(entry);
    }

    json["all_packaged_models_supported"] = all_packaged_models_supported;
    json["healthy"] = any_packaged_model_found && all_packaged_models_supported;
#endif

    return json;
}

nlohmann::json inspect_mlx_model_pack(const std::filesystem::path& models_dir) {
#if !defined(__APPLE__)
    (void)models_dir;
#endif
    nlohmann::json json;
    json["applicable"] = false;
    json["probe_available"] = core::mlx_probe_available();
    json["primary_pack_ready"] = false;
    json["bridge_ready"] = false;
    json["integration_mode"] = "mlx_pack_with_bridge_exports";
    json["backend_integrated"] = false;
    json["healthy"] = false;
    json["models"] = nlohmann::json::array();
    json["primary_artifacts"] = nlohmann::json::array();
    json["bridge_artifacts"] = nlohmann::json::array();

#if defined(__APPLE__)
    json["applicable"] = true;

    bool any_primary_found = false;
    bool all_primary_probe_ready = true;

    for (const auto& model : model_catalog()) {
        if (model.recommended_backend != "mlx") {
            continue;
        }

        std::filesystem::path model_path = models_dir / model.filename;
        bool found = std::filesystem::exists(model_path);

        nlohmann::json entry;
        entry["filename"] = model.filename;
        entry["found"] = found;
        entry["artifact_family"] = model.artifact_family;
        entry["recommended_backend"] = model.recommended_backend;
        entry["validated_platforms"] = model.validated_platforms;
        entry["validated_hardware_tiers"] = model.validated_hardware_tiers;
        entry["probe_ready"] = false;
        entry["error"] = "";

        if (!found) {
            json["models"].push_back(entry);
            if (model.artifact_family == "safetensors") {
                all_primary_probe_ready = false;
                json["primary_artifacts"].push_back(entry);
            }
            continue;
        }

        entry["size_bytes"] = std::filesystem::file_size(model_path);

        if (model.artifact_family == "safetensors") {
            any_primary_found = true;
            auto probe_res = core::probe_mlx_weights(model_path);
            entry["probe_ready"] = probe_res.has_value();
            entry["metadata_readable"] = probe_res.has_value();
            if (!probe_res) {
                entry["error"] = probe_res.error().message;
                all_primary_probe_ready = false;
            }
            json["primary_artifacts"].push_back(entry);
        }

        json["models"].push_back(entry);
    }

    bool any_bridge_found = false;
    bool all_bridge_importable = true;
    std::error_code error;
    if (std::filesystem::exists(models_dir, error)) {
        for (const auto& item : std::filesystem::directory_iterator(models_dir, error)) {
            if (error || !item.is_regular_file() || item.path().extension() != ".mlxfn") {
                continue;
            }
            if (is_metadata_sidecar(item.path())) {
                continue;
            }

            any_bridge_found = true;
            nlohmann::json entry;
            entry["filename"] = item.path().filename().string();
            entry["found"] = true;
            entry["artifact_family"] = "mlxfn";
            entry["recommended_backend"] = "mlx";
            entry["probe_ready"] = false;
            entry["importable"] = false;
            entry["size_bytes"] = std::filesystem::file_size(item.path(), error);
            entry["error"] = "";

            auto probe_res = core::probe_mlx_function(item.path());
            entry["probe_ready"] = probe_res.has_value();
            entry["importable"] = probe_res.has_value();
            if (!probe_res) {
                entry["error"] = probe_res.error().message;
                all_bridge_importable = false;
            }

            json["bridge_artifacts"].push_back(entry);
            json["models"].push_back(entry);
        }
    }

    json["primary_pack_ready"] = any_primary_found && all_primary_probe_ready;
    json["bridge_ready"] = core::mlx_probe_available() && any_bridge_found && all_bridge_importable;
    json["backend_integrated"] = json["bridge_ready"];
    json["healthy"] = json["backend_integrated"] && json["primary_pack_ready"];
#endif

    return json;
}

nlohmann::json inspect_windows_rtx_track(const std::filesystem::path& models_dir) {
    nlohmann::json json;
    json["applicable"] = false;
    json["gpu_detected"] = false;
    json["provider_available"] = false;
    json["ampere_or_newer"] = false;
    json["runtime_cache_ready"] = false;
    json["backend_integrated"] = false;
    json["healthy"] = false;
    json["driver_query_available"] = false;
    json["driver_version"] = "";
    json["gpu_name"] = "";
    json["gpu_memory_mb"] = 0;
    json["packaged_models"] = nlohmann::json::array();
    json["compiled_context_models"] = nlohmann::json::array();

#if !defined(_WIN32)
    (void)models_dir;
#endif

#if defined(_WIN32)
    json["applicable"] = true;

    auto gpu = core::probe_windows_rtx_gpu();
    json["gpu_detected"] = gpu.has_value();
    if (gpu.has_value()) {
        json["gpu_name"] = gpu->adapter_name;
        json["gpu_memory_mb"] = gpu->dedicated_memory_mb;
        json["driver_query_available"] = gpu->driver_query_available;
        json["driver_version"] = gpu->driver_version;
        json["provider_available"] = gpu->provider_available;
        json["ampere_or_newer"] = gpu->ampere_or_newer;
    }

    auto runtime_cache_dir = common::tensorrt_rtx_runtime_cache_root();
    json["runtime_cache_dir"] =
        runtime_cache_dir.has_value() ? runtime_cache_dir->string() : std::string();
    json["runtime_cache_ready"] = runtime_cache_dir.has_value();
    json["backend_integrated"] = gpu.has_value() && gpu->provider_available && gpu->ampere_or_newer;

    bool packaged_models_ready = true;
    bool any_packaged_model_found = false;
    std::error_code error;
    if (std::filesystem::exists(models_dir, error)) {
        for (const auto& item : std::filesystem::directory_iterator(models_dir, error)) {
            if (error || !item.is_regular_file()) {
                continue;
            }
            if (item.path().extension() == ".onnx" &&
                item.path().stem().string().find("_ctx") != std::string::npos) {
                json["compiled_context_models"].push_back(item.path().filename().string());
            }
        }
    }

    for (const auto& model : model_catalog()) {
        if (!model.packaged_for_windows) {
            continue;
        }

        std::filesystem::path model_path = models_dir / model.filename;
        bool found = std::filesystem::exists(model_path);
        any_packaged_model_found = any_packaged_model_found || found;
        packaged_models_ready = packaged_models_ready && found;

        nlohmann::json entry;
        entry["filename"] = model.filename;
        entry["found"] = found;
        entry["recommended_backend"] = model.recommended_backend;
        entry["validated_platforms"] = model.validated_platforms;
        entry["intended_platforms"] = model.intended_platforms;
        if (model.recommended_backend == "tensorrt") {
            auto compiled_context_path =
                common::existing_tensorrt_rtx_compiled_context_model_path(model_path);
            entry["compiled_context_path"] =
                compiled_context_path.has_value() ? compiled_context_path->string() : std::string();
            entry["compiled_context_found"] = compiled_context_path.has_value();
        }
        json["packaged_models"].push_back(entry);
    }

    json["packaged_models_ready"] = any_packaged_model_found && packaged_models_ready;
    json["healthy"] = json["backend_integrated"].get<bool>() &&
                      json["runtime_cache_ready"].get<bool>() &&
                      json["packaged_models_ready"].get<bool>();
#endif

    return json;
}

nlohmann::json latency_summary(const std::vector<double>& samples) {
    nlohmann::json json;
    json["count"] = samples.size();
    if (samples.empty()) {
        json["min_ms"] = 0.0;
        json["max_ms"] = 0.0;
        json["avg_ms"] = 0.0;
        json["p50_ms"] = 0.0;
        json["p95_ms"] = 0.0;
        return json;
    }

    std::vector<double> ordered = samples;
    std::sort(ordered.begin(), ordered.end());

    auto percentile = [&](double fraction) {
        size_t index = static_cast<size_t>(fraction * static_cast<double>(ordered.size() - 1));
        return ordered[index];
    };

    double total = 0.0;
    for (double sample : ordered) {
        total += sample;
    }

    json["min_ms"] = ordered.front();
    json["max_ms"] = ordered.back();
    json["avg_ms"] = total / static_cast<double>(ordered.size());
    json["p50_ms"] = percentile(0.50);
    json["p95_ms"] = percentile(0.95);
    return json;
}

}  // namespace

nlohmann::json inspect_operational_health(const std::filesystem::path& models_dir) {
    auto executable_path = current_executable_path();

    nlohmann::json json;
    json["executable"]["path"] = executable_path.string();
    json["executable"]["directory"] = executable_path.parent_path().string();
    json["bundle"] = inspect_bundle(models_dir, executable_path);
    json["video"] = inspect_video_stack();
    json["cache"] = inspect_cache();
    json["coreml"] = inspect_coreml_execution_provider(models_dir);
    json["mlx"] = inspect_mlx_model_pack(models_dir);
    json["windows_rtx"] = inspect_windows_rtx_track(models_dir);
    return json;
}

nlohmann::json summarize_latency_samples(const std::vector<double>& samples) {
    return latency_summary(samples);
}

}  // namespace corridorkey::app
