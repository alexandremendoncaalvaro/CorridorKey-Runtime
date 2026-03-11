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
    std::filesystem::path smoke_test_path = bundle_root / "smoke_test.sh";

    auto runtime_library = find_runtime_library(executable_dir);
    auto references = dependency_references(executable_path);
    bool runtime_reference_found =
        std::any_of(references.begin(), references.end(), [](const std::string& reference) {
            return reference.find("libonnxruntime") != std::string::npos ||
                   reference.find("onnxruntime.dll") != std::string::npos;
        });

    nlohmann::json packaged_models = nlohmann::json::array();
    bool packaged_models_present = true;
    for (const auto& model : model_catalog()) {
        if (!model.packaged_for_macos) {
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
    json["dependency_references"] = references;
    json["packaged_models"] = packaged_models;
    json["signature"] = inspect_signature(executable_path);
    json["healthy"] = packaged_layout_detected && runtime_library.has_value() &&
                      runtime_reference_found && packaged_models_present;
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
    json["healthy"] = selected_cache_dir.has_value();
    return json;
}

nlohmann::json inspect_coreml_execution_provider(const std::filesystem::path& models_dir) {
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
        if (!model.packaged_for_macos) {
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
    nlohmann::json json;
    json["applicable"] = false;
    json["probe_available"] = core::mlx_probe_available();
    json["healthy"] = false;
    json["all_candidate_models_importable"] = false;
    json["models"] = nlohmann::json::array();

#if defined(__APPLE__)
    json["applicable"] = true;

    bool all_candidate_models_importable = true;
    bool any_candidate_model_found = false;

    for (const auto& model : model_catalog()) {
        if (model.artifact_family != "mlxfn") {
            continue;
        }

        std::filesystem::path model_path = models_dir / model.filename;
        bool found = std::filesystem::exists(model_path);
        any_candidate_model_found = any_candidate_model_found || found;

        nlohmann::json entry;
        entry["filename"] = model.filename;
        entry["found"] = found;
        entry["artifact_family"] = model.artifact_family;
        entry["recommended_backend"] = model.recommended_backend;
        entry["validated_platforms"] = model.validated_platforms;
        entry["validated_hardware_tiers"] = model.validated_hardware_tiers;
        entry["importable"] = false;
        entry["error"] = "";

        if (!found) {
            all_candidate_models_importable = false;
            json["models"].push_back(entry);
            continue;
        }

        auto probe_res = core::probe_mlx_function(model_path);
        entry["importable"] = probe_res.has_value();
        if (!probe_res) {
            entry["error"] = probe_res.error().message;
            all_candidate_models_importable = false;
        }

        json["models"].push_back(entry);
    }

    json["all_candidate_models_importable"] = all_candidate_models_importable;
    json["healthy"] =
        core::mlx_probe_available() && any_candidate_model_found && all_candidate_models_importable;
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
    return json;
}

nlohmann::json summarize_latency_samples(const std::vector<double>& samples) {
    return latency_summary(samples);
}

}  // namespace corridorkey::app
