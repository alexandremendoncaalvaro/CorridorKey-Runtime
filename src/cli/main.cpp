#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <corridorkey/engine.hpp>
#include <corridorkey/version.hpp>
#include <cstdio>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "../app/hardware_profile.hpp"
#include "../app/job_orchestrator.hpp"
#include "../app/model_compiler.hpp"
#include "../app/ofx_runtime_service.hpp"
#include "../app/runtime_contracts.hpp"
#include "../common/json_utils.hpp"
#include "../common/local_ipc.hpp"
#include "../common/runtime_paths.hpp"
#include "../core/windows_rtx_probe.hpp"
#include "../frame_io/video_io.hpp"
#include "device_selection.hpp"
#include "process_paths.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

std::string backend_to_string_local(Backend backend) {
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

nlohmann::json event_to_json(const JobEvent& event) {
    nlohmann::json json;

    switch (event.type) {
        case JobEventType::JobStarted:
            json["type"] = "job_started";
            break;
        case JobEventType::BackendSelected:
            json["type"] = "backend_selected";
            break;
        case JobEventType::Progress:
            json["type"] = "progress";
            break;
        case JobEventType::Warning:
            json["type"] = "warning";
            break;
        case JobEventType::ArtifactWritten:
            json["type"] = "artifact_written";
            break;
        case JobEventType::Completed:
            json["type"] = "completed";
            break;
        case JobEventType::Failed:
            json["type"] = "failed";
            break;
        case JobEventType::Cancelled:
            json["type"] = "cancelled";
            break;
    }

    json["phase"] = event.phase;
    json["progress"] = event.progress;
    if (event.backend != Backend::Auto) {
        json["backend"] = backend_to_string_local(event.backend);
    }
    if (!event.message.empty()) {
        json["message"] = event.message;
    }
    if (!event.artifact_path.empty()) {
        json["artifact_path"] = event.artifact_path;
    }
    if (event.error.has_value()) {
        json["error"]["code"] = static_cast<int>(event.error->code);
        json["error"]["message"] = event.error->message;
    }
    if (event.fallback.has_value()) {
        json["fallback"]["requested_backend"] =
            backend_to_string_local(event.fallback->requested_backend);
        json["fallback"]["selected_backend"] =
            backend_to_string_local(event.fallback->selected_backend);
        json["fallback"]["reason"] = event.fallback->reason;
    }
    if (!event.timings.empty()) {
        json["timings"] = nlohmann::json::array();
        for (const auto& timing : event.timings) {
            nlohmann::json timing_json;
            timing_json["name"] = timing.name;
            timing_json["total_ms"] = timing.total_ms;
            timing_json["sample_count"] = timing.sample_count;
            timing_json["work_units"] = timing.work_units;
            timing_json["avg_ms"] =
                timing.sample_count > 0 ? timing.total_ms / timing.sample_count : 0.0;
            if (timing.work_units > 0) {
                timing_json["ms_per_unit"] = timing.total_ms / timing.work_units;
            }
            json["timings"].push_back(std::move(timing_json));
        }
    }

    return json;
}

bool option_present(int argc, char* argv[], std::initializer_list<std::string_view> option_names) {
    for (int index = 1; index < argc; ++index) {
        std::string_view token(argv[index]);
        for (std::string_view name : option_names) {
            if (token == name) {
                return true;
            }
            if (token.size() > name.size() && token.substr(0, name.size()) == name &&
                token[name.size()] == '=') {
                return true;
            }
        }
    }

    return false;
}

std::vector<std::string> positional_args(const cxxopts::ParseResult& result) {
    if (!result.count("args")) {
        return {};
    }
    return result["args"].as<std::vector<std::string>>();
}

std::optional<std::string> selected_preset_selector(const cxxopts::ParseResult& result) {
    if (result.count("quality")) {
        return result["quality"].as<std::string>();
    }
    if (result.count("preset")) {
        return result["preset"].as<std::string>();
    }
    return std::nullopt;
}

std::string normalized_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

Result<VideoOutputOptions> resolve_video_output_options(const cxxopts::ParseResult& result) {
    VideoOutputOptions options;
    if (!result.count("video-encode")) {
        return options;
    }

    std::string mode = normalized_lower(result["video-encode"].as<std::string>());
    if (mode == "lossless") {
        options.mode = VideoOutputMode::Lossless;
        return options;
    }
    if (mode == "balanced") {
        options.mode = VideoOutputMode::Balanced;
        return options;
    }

    return Unexpected<Error>{Error{ErrorCode::InvalidParameters,
                                   "Invalid --video-encode value. Use 'lossless' or 'balanced'."}};
}

Result<QualityFallbackMode> parse_quality_fallback_mode(const std::string& value) {
    const std::string normalized = normalized_lower(value);
    if (normalized == "auto") {
        return QualityFallbackMode::Auto;
    }
    if (normalized == "direct") {
        return QualityFallbackMode::Direct;
    }
    if (normalized == "coarse_to_fine") {
        return QualityFallbackMode::CoarseToFine;
    }
    return Unexpected<Error>{Error{
        ErrorCode::InvalidParameters,
        "Invalid --quality-fallback value. Use 'auto', 'direct', or 'coarse_to_fine'.",
    }};
}

Result<RefinementMode> parse_refinement_mode(const std::string& value) {
    const std::string normalized = normalized_lower(value);
    if (normalized == "auto") {
        return RefinementMode::Auto;
    }
    if (normalized == "full_frame") {
        return RefinementMode::FullFrame;
    }
    if (normalized == "tiled") {
        return RefinementMode::Tiled;
    }
    return Unexpected<Error>{Error{
        ErrorCode::InvalidParameters,
        "Invalid --refinement-mode value. Use 'auto', 'full_frame', or 'tiled'.",
    }};
}

Result<PrecisionPreference> parse_precision_preference(const std::string& value) {
    const std::string normalized = normalized_lower(value);
    if (normalized == "auto") {
        return PrecisionPreference::Auto;
    }
    if (normalized == "fp16") {
        return PrecisionPreference::FP16;
    }
    if (normalized == "int8") {
        return PrecisionPreference::Int8;
    }
    return Unexpected<Error>{Error{
        ErrorCode::InvalidParameters,
        "Invalid --precision value. Use 'auto', 'fp16', or 'int8'.",
    }};
}

std::string precision_preference_to_string(PrecisionPreference preference) {
    switch (preference) {
        case PrecisionPreference::FP16:
            return "fp16";
        case PrecisionPreference::Int8:
            return "int8";
        case PrecisionPreference::Auto:
        default:
            return "auto";
    }
}

std::string artifact_precision_label(const std::filesystem::path& model_path) {
    const std::string filename = normalized_lower(model_path.filename().string());
    if (filename.find("_fp16_") != std::string::npos) {
        return "fp16";
    }
    if (filename.find("_int8_") != std::string::npos) {
        return "int8";
    }
    if (filename.find("_mlx") != std::string::npos || model_path.extension() == ".mlxfn" ||
        model_path.extension() == ".safetensors") {
        return "mlx";
    }
    return "auto";
}

ArtifactVariantPreference artifact_variant_preference_for_precision(
    PrecisionPreference preference) {
    switch (preference) {
        case PrecisionPreference::FP16:
            return ArtifactVariantPreference::FP16;
        case PrecisionPreference::Int8:
            return ArtifactVariantPreference::Int8;
        case PrecisionPreference::Auto:
        default:
            return ArtifactVariantPreference::Auto;
    }
}

Result<int> parse_coarse_resolution_override(const std::string& value) {
    if (value == "0") {
        return 0;
    }
    const int resolution = std::stoi(value);
    switch (resolution) {
        case 512:
        case 768:
        case 1024:
        case 1536:
        case 2048:
            return resolution;
        default:
            break;
    }
    return Unexpected<Error>{Error{
        ErrorCode::InvalidParameters,
        "Invalid --coarse-resolution value. Use 0, 512, 768, 1024, 1536, or 2048.",
    }};
}

InferenceParams build_inference_params(const cxxopts::ParseResult& result,
                                       const std::optional<InferenceParams>& base_params, int argc,
                                       char* argv[]) {
    InferenceParams params = base_params.value_or(InferenceParams{});

    if (!base_params.has_value() || option_present(argc, argv, {"--resolution", "-r"})) {
        params.target_resolution = result["resolution"].as<int>();
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--despill"})) {
        params.despill_strength = result["despill"].as<float>();
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--batch-size"})) {
        params.batch_size = result["batch-size"].as<int>();
    }
    if (option_present(argc, argv, {"--despeckle"})) {
        params.auto_despeckle = true;
    }
    if (option_present(argc, argv, {"--tiled"})) {
        params.enable_tiling = true;
    }

    if (!base_params.has_value() || option_present(argc, argv, {"--quality-fallback"})) {
        auto fallback_mode =
            parse_quality_fallback_mode(result["quality-fallback"].as<std::string>());
        if (!fallback_mode) {
            throw std::runtime_error(fallback_mode.error().message);
        }
        params.quality_fallback_mode = *fallback_mode;
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--refinement-mode"})) {
        auto refinement_mode = parse_refinement_mode(result["refinement-mode"].as<std::string>());
        if (!refinement_mode) {
            throw std::runtime_error(refinement_mode.error().message);
        }
        params.refinement_mode = *refinement_mode;
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--coarse-resolution"})) {
        auto coarse_resolution =
            parse_coarse_resolution_override(result["coarse-resolution"].as<std::string>());
        if (!coarse_resolution) {
            throw std::runtime_error(coarse_resolution.error().message);
        }
        params.coarse_resolution_override = *coarse_resolution;
    }
    if (!base_params.has_value() || option_present(argc, argv, {"--precision"})) {
        auto precision = parse_precision_preference(result["precision"].as<std::string>());
        if (!precision) {
            throw std::runtime_error(precision.error().message);
        }
        params.precision_preference = *precision;
    }

    if (params.target_resolution > 0) {
        params.requested_quality_resolution = params.target_resolution;
    }

    return params;
}

std::filesystem::path default_models_dir() {
    return common::default_models_root();
}

bool is_video_path(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    return extension == ".mp4" || extension == ".mov" || extension == ".avi" || extension == ".mkv";
}

Result<std::filesystem::path> default_output_path_for_input(
    const std::filesystem::path& input_path, const VideoOutputOptions& output_options) {
    std::filesystem::path outputs_root = "outputs";

    if (std::filesystem::is_directory(input_path)) {
        auto name = input_path.filename().string();
        if (name.empty()) {
            name = "sequence";
        }
        return outputs_root / (name + "_out");
    }

    if (is_video_path(input_path)) {
        auto reader_res = VideoReader::open(input_path);
        if (!reader_res) {
            return Unexpected(reader_res.error());
        }
        VideoFrameFormat input_format = (*reader_res)->format();
        auto base_path = outputs_root / (input_path.stem().string() + "_out");
        return resolve_video_output_path(base_path, output_options, input_format);
    }

    return outputs_root / input_path.stem();
}

struct ResolvedExecution {
    std::filesystem::path models_dir = {};
    std::filesystem::path model_path = {};
    std::optional<PresetDefinition> preset = std::nullopt;
    InferenceParams params = {};
    bool default_model_selected = false;
    bool default_output_selected = false;
};

Result<ResolvedExecution> resolve_execution_defaults(const cxxopts::ParseResult& result, int argc,
                                                     char* argv[], const DeviceInfo& device,
                                                     bool requires_output) {
    ResolvedExecution resolved;
    resolved.models_dir = default_models_dir();
    if (result.count("model")) {
        resolved.model_path = result["model"].as<std::string>();
        auto parent = resolved.model_path.parent_path();
        if (!parent.empty()) {
            resolved.models_dir = parent;
        }
    }

    auto capabilities = runtime_capabilities();
    if (auto preset_selector = selected_preset_selector(result); preset_selector.has_value()) {
        resolved.preset = find_preset_by_selector(*preset_selector);
        if (!resolved.preset.has_value()) {
            return Unexpected<Error>{
                Error{ErrorCode::InvalidParameters, "Unknown preset: " + *preset_selector}};
        }
    } else if (!result.count("model")) {
        resolved.preset = default_preset_for_capabilities(capabilities);
    }

    if (result.count("model")) {
        resolved.params = build_inference_params(result, std::nullopt, argc, argv);
        if (resolved.params.requested_quality_resolution <= 0) {
            resolved.params.requested_quality_resolution =
                app::packaged_model_resolution(result["model"].as<std::string>())
                    .value_or(resolved.params.target_resolution);
        }
        auto explicit_model =
            app::resolve_model_artifact_for_request(resolved.model_path, resolved.params, device);
        if (!explicit_model) {
            return Unexpected<Error>(explicit_model.error());
        }
        resolved.model_path = *explicit_model;
    } else {
        resolved.params = build_inference_params(
            result,
            resolved.preset.has_value() ? std::optional<InferenceParams>{resolved.preset->params}
                                        : std::nullopt,
            argc, argv);

        auto selected_model = default_model_for_request(
            capabilities, device, resolved.preset,
            artifact_variant_preference_for_precision(resolved.params.precision_preference));
        if (!selected_model.has_value()) {
            return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                           "Could not resolve a default model for this device."}};
        }

        resolved.model_path = resolved.models_dir / selected_model->filename;
        const int requested_resolution = resolved.params.requested_quality_resolution > 0
                                             ? resolved.params.requested_quality_resolution
                                             : selected_model->resolution;
        resolved.params.requested_quality_resolution = requested_resolution;
        auto effective_model =
            app::resolve_model_artifact_for_request(resolved.model_path, resolved.params, device);
        if (!effective_model) {
            return Unexpected<Error>(effective_model.error());
        }
        resolved.model_path = *effective_model;
        if (!std::filesystem::exists(resolved.model_path)) {
            return Unexpected<Error>{
                Error{ErrorCode::ModelLoadFailed,
                      "Default model pack not found: " + resolved.model_path.string()}};
        }
        resolved.default_model_selected = true;
    }

    if (requires_output && !result.count("output")) {
        resolved.default_output_selected = true;
    }

    return resolved;
}

void print_stage_timings(const nlohmann::json& timings) {
    if (!timings.is_array() || timings.empty()) {
        return;
    }

    std::cout << "Stage timings:\n";
    for (const auto& timing : timings) {
        std::cout << " - " << timing["name"].get<std::string>() << ": "
                  << timing["total_ms"].get<double>() << " ms";
        if (timing.contains("avg_ms")) {
            std::cout << " (avg " << timing["avg_ms"].get<double>() << " ms";
            if (timing.contains("sample_count")) {
                std::cout << " over " << timing["sample_count"].get<std::uint64_t>() << " samples";
            }
            std::cout << ")";
        }
        if (timing.contains("ms_per_unit")) {
            std::cout << ", " << timing["ms_per_unit"].get<double>() << " ms/unit";
        }
        std::cout << "\n";
    }
}

void print_benchmark_artifact_summary(const nlohmann::json& report) {
    if (report.contains("artifact")) {
        std::cout << "Artifact: " << report["artifact"].get<std::string>() << "\n";
    }
    if (report.contains("requested_precision")) {
        std::cout << "Requested precision: " << report["requested_precision"].get<std::string>()
                  << "\n";
    }
    if (report.contains("effective_precision")) {
        std::cout << "Effective precision: " << report["effective_precision"].get<std::string>()
                  << "\n";
    }
    if (report.contains("requested_resolution") && report.contains("effective_resolution")) {
        std::cout << "Requested resolution: " << report["requested_resolution"].get<int>() << "\n"
                  << "Effective resolution: " << report["effective_resolution"].get<int>() << "\n";
    }
    if (report.contains("safe_quality_ceiling")) {
        std::cout << "Safe quality ceiling: " << report["safe_quality_ceiling"].get<int>() << "\n";
    }
    if (report.value("manual_override_above_safe_ceiling", false)) {
        std::cout << "Manual override: above safe ceiling\n";
    }
    if (report.value("quality_fallback_used", false)) {
        std::cout << "Quality fallback: "
                  << report.value("quality_fallback_mode", std::string("auto")) << "\n";
    }
    if (report.contains("precision_fallback") && report["precision_fallback"].is_object()) {
        std::cout << "Precision fallback: "
                  << report["precision_fallback"].value("requested", std::string("auto")) << " -> "
                  << report["precision_fallback"].value("effective", std::string("auto")) << "\n";
    }
}

}  // namespace

void print_info() {
    auto info = JobOrchestrator::get_system_info();
    std::cout << "CorridorKey Runtime v" << CORRIDORKEY_DISPLAY_VERSION_STRING << "\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Detected Hardware Devices:\n";

#if defined(_WIN32)
    auto gpus = corridorkey::core::list_windows_gpus();
    if (gpus.empty()) {
        std::cout << " - No compatible GPUs found. Using CPU fallback.\n";
    } else {
        for (const auto& gpu : gpus) {
            std::cout << " - " << gpu.adapter_name;
            if (gpu.tensorrt_rtx_available) std::cout << " [TensorRT]";
            if (gpu.cuda_available) std::cout << " [CUDA]";
            if (gpu.directml_available) std::cout << " [DirectML]";
            if (gpu.dedicated_memory_mb > 0) std::cout << " (" << gpu.dedicated_memory_mb << " MB)";
            std::cout << "\n";
        }
    }
#else
    for (const auto& d : info["devices"]) {
        std::cout << " - " << std::left << std::setw(30) << d["name"].get<std::string>() << " ["
                  << d["backend"].get<std::string>() << "] "
                  << (d["memory_mb"].get<int64_t>() > 0
                          ? (std::to_string(d["memory_mb"].get<int64_t>()) + " MB")
                          : "")
                  << "\n";
    }
#endif

    std::cout << "Capabilities:\n"
              << " - Multi-GPU (Universal): yes\n"
              << " - TensorRT RTX track: yes\n"
              << " - CPU fallback: yes\n"
              << " - Default video mode: "
              << info["capabilities"]["default_video_mode"].get<std::string>() << "\n"
              << " - Default video container: "
              << info["capabilities"]["default_video_container"].get<std::string>() << "\n"
              << " - Default video encoder: "
              << info["capabilities"]["default_video_encoder"].get<std::string>() << "\n";
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("corridorkey",
                             "CorridorKey Runtime - High-performance neural green screen keyer");

    options.add_options()(
        "command",
        "Sub-command to run (info, process, download, doctor, benchmark, models, presets, "
        "compile-context, ofx-runtime-server)",
        cxxopts::value<std::string>())("args", "Positional arguments",
                                       cxxopts::value<std::vector<std::string>>())(
        "i,input", "Input video or directory of frames", cxxopts::value<std::string>())(
        "a,alpha-hint", "Alpha hint video or directory (optional)", cxxopts::value<std::string>())(
        "o,output", "Output directory or file (optional for process/benchmark)",
        cxxopts::value<std::string>())("m,model",
                                       "Path to model pack or exported artifact (advanced)",
                                       cxxopts::value<std::string>())(
        "preset", "Preset (preview, balanced, max)", cxxopts::value<std::string>())(
        "quality", "Alias for --preset", cxxopts::value<std::string>())(
        "r,resolution", "Resolution (0=auto, 512, 768, 1024, 1536, 2048)",
        cxxopts::value<int>()->default_value("0"))(
        "quality-fallback", "Quality fallback mode (auto, direct, coarse_to_fine)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "refinement-mode", "Validated refinement strategy override (auto, full_frame, tiled)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "precision", "Runtime precision preference for process/benchmark (auto, fp16, int8)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "coarse-resolution", "Coarse artifact override (0, 512, 768, 1024, 1536, 2048)",
        cxxopts::value<std::string>()->default_value("0"))(
        "d,device", "Device (auto, cpu, tensorrt, rtx, mlx, coreml, cuda, dml)",
        cxxopts::value<std::string>()->default_value("auto"))(
        "endpoint-port", "Local OFX runtime server control port", cxxopts::value<int>())(
        "idle-timeout-ms", "Local OFX runtime server idle timeout in milliseconds",
        cxxopts::value<int>())("video-encode", "Video output encoding (lossless, balanced)",
                               cxxopts::value<std::string>()->default_value("lossless"))(
        "variant", "ONNX model variant for download only (int8, fp16, fp32)",
        cxxopts::value<std::string>())("batch-size",
                                       "Number of frames to process in a single GPU call",
                                       cxxopts::value<int>()->default_value("1"))(
        "despill", "Green spill removal (0.0-1.0)", cxxopts::value<float>()->default_value("0.5"))(
        "despeckle", "Enable morphological cleanup")("tiled", "Enable tiling for high-res (4K+)")(
        "json", "Output results in JSON format")("v,version", "Print version")(
        "h,help", "Print detailed help");

    options.parse_positional({"command", "args"});
    options.positional_help("command [input] [output]");

    if (argc <= 1) {
        std::cout << "==========================================\n"
                  << "      CorridorKey Runtime v" << CORRIDORKEY_DISPLAY_VERSION_STRING << "\n"
                  << "==========================================\n\n"
                  << "Quick start:\n\n"
                  << "1. Check the runtime:\n"
                  << "   corridorkey doctor\n\n"
                  << "2. Process a video with the default accelerated path:\n"
                  << "   corridorkey process input.mp4 output.mp4\n\n"
                  << "3. Use a simpler or stronger preset when needed:\n"
                  << "   corridorkey process input.mp4 output.mp4 --preset preview\n"
                  << "   corridorkey process input.mp4 output.mp4 --preset max\n\n"
                  << "4. Prepare TensorRT RTX context models for a Windows bundle (maintainers):\n"
                  << "   corridorkey compile-context --model models/corridorkey_fp16_1024.onnx "
                     "--device tensorrt\n\n"
                  << "5. Inspect validated models and presets:\n"
                  << "   corridorkey models\n"
                  << "   corridorkey presets\n\n"
                  << "Run 'corridorkey --help' for all options.\n"
                  << std::endl;
        return 0;
    }

    try {
        auto result = options.parse(argc, argv);
        bool use_json = result.count("json");

        if (use_json) {
#if defined(_WIN32)
            FILE* suppressed_stderr = nullptr;
            freopen_s(&suppressed_stderr, "NUL", "a", stderr);
#else
            std::freopen("/dev/null", "a", stderr);
#endif
        }

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("version")) {
            if (use_json) {
                std::cout << common::safe_json_dump(
                                 nlohmann::json({{"version", CORRIDORKEY_DISPLAY_VERSION_STRING},
                                                 {"base_version", CORRIDORKEY_VERSION_STRING}}))
                          << std::endl;
            } else {
                std::cout << "CorridorKey Runtime v" << CORRIDORKEY_DISPLAY_VERSION_STRING
                          << std::endl;
            }
            return 0;
        }

        if (!result.count("command")) {
            std::cout
                << "Error: No command specified. Use 'info', 'download', 'process', 'doctor', "
                   "'benchmark', 'models', 'presets', or 'compile-context'."
                << std::endl;
            return 1;
        }

        std::string cmd = result["command"].as<std::string>();
        auto args = positional_args(result);

        if (cmd == "info") {
            if (use_json) {
                std::cout << common::safe_json_dump(JobOrchestrator::get_system_info(), 4)
                          << std::endl;
            } else {
                print_info();
            }
            return 0;
        }

        if (cmd == "doctor") {
            std::filesystem::path models_dir = default_models_dir();
            if (result.count("model")) {
                models_dir = std::filesystem::path(result["model"].as<std::string>()).parent_path();
            }
            auto report = JobOrchestrator::run_doctor(models_dir);
            if (use_json) {
                std::cout << common::safe_json_dump(report, 4) << std::endl;
            } else {
                std::cout << "--- CorridorKey Doctor Report ---\n"
                          << "Version: " << report["system"]["version"].get<std::string>() << "\n"
                          << "Detected Devices: " << report["system"]["devices"].size() << "\n";
                for (const auto& m : report["models"]) {
                    if (m["found"].get<bool>()) {
                        std::cout << " [OK] Model: " << m["variant"].get<std::string>() << "_"
                                  << m["resolution"].get<int>() << "\n";
                    }
                }
            }
            return 0;
        }

        if (cmd == "models") {
            auto models = JobOrchestrator::list_models();
            if (use_json) {
                std::cout << common::safe_json_dump(models, 4) << std::endl;
            } else {
                std::cout << "--- Model Catalog ---\n";
                for (const auto& model : models) {
                    std::cout << " - " << model["filename"].get<std::string>();
                    if (model["validated_for_macos"].get<bool>()) {
                        std::cout << " [validated-macos]";
                    }
                    if (model["packaged_for_macos"].get<bool>()) {
                        std::cout << " [packaged]";
                    }
                    if (model["packaged_for_windows"].get<bool>()) {
                        std::cout << " [packaged-windows]";
                    }
                    std::cout << "\n   " << model["description"].get<std::string>() << "\n";
                }
            }
            return 0;
        }

        if (cmd == "presets") {
            auto presets = JobOrchestrator::list_presets();
            if (use_json) {
                std::cout << common::safe_json_dump(presets, 4) << std::endl;
            } else {
                std::cout << "--- Presets ---\n";
                for (const auto& preset : presets) {
                    std::cout << " - " << preset["name"].get<std::string>();
                    if (preset["default_for_macos"].get<bool>()) {
                        std::cout << " [default-macos]";
                    }
                    if (preset["default_for_windows"].get<bool>()) {
                        std::cout << " [default-windows]";
                    }
                    std::cout << "\n   " << preset["description"].get<std::string>() << "\n";
                }
            }
            return 0;
        }

        if (cmd == "compile-context") {
            std::filesystem::path model_path =
                result.count("model") ? std::filesystem::path(result["model"].as<std::string>())
                                      : std::filesystem::path{};
            if (model_path.empty()) {
                std::cerr << "Error: 'compile-context' requires --model." << std::endl;
                return 1;
            }

            auto devices = list_devices();
            DeviceInfo device = cli::select_device(devices, result["device"].as<std::string>());
            if (device.backend == Backend::Auto) {
                device = auto_detect();
            }

            std::filesystem::path output_path =
                result.count("output")
                    ? std::filesystem::path(result["output"].as<std::string>())
                    : common::tensorrt_rtx_compiled_context_model_path(model_path);

            auto compile_res = compile_tensorrt_rtx_context_model(model_path, output_path, device);
            if (!compile_res) {
                if (use_json) {
                    nlohmann::json failure;
                    failure["command"] = "compile-context";
                    failure["success"] = false;
                    failure["error"] = compile_res.error().message;
                    std::cout << common::safe_json_dump(failure, 4) << std::endl;
                } else {
                    std::cerr << "Error: " << compile_res.error().message << std::endl;
                }
                return 1;
            }

            if (use_json) {
                nlohmann::json success;
                success["command"] = "compile-context";
                success["success"] = true;
                success["input_model"] = model_path.string();
                success["output_model"] = compile_res->string();
                success["backend"] = backend_to_string_local(device.backend);
                std::cout << common::safe_json_dump(success, 4) << std::endl;
            } else {
                std::cout << "Compiled TensorRT RTX context model: " << compile_res->string()
                          << std::endl;
            }
            return 0;
        }

        if (cmd == "ofx-runtime-server") {
            app::OfxRuntimeServiceOptions service_options;
            service_options.endpoint = common::default_ofx_runtime_endpoint();
            service_options.log_path = common::ofx_runtime_server_log_path();
            if (result.count("endpoint-port")) {
                service_options.endpoint.port =
                    static_cast<std::uint16_t>(result["endpoint-port"].as<int>());
            }
            if (result.count("idle-timeout-ms")) {
                service_options.idle_timeout =
                    std::chrono::milliseconds(result["idle-timeout-ms"].as<int>());
            }

            auto service_result = app::OfxRuntimeService::run(service_options);
            if (!service_result) {
                if (!use_json) {
                    std::cerr << "Error: " << service_result.error().message << std::endl;
                }
                return 1;
            }
            return 0;
        }

        if (cmd == "benchmark") {
            if (args.size() > 1) {
                std::cout << "Error: 'benchmark' accepts at most one positional input path."
                          << std::endl;
                return 1;
            }

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            DeviceInfo device = cli::select_device(devices, device_str);
            auto video_output_options_res = resolve_video_output_options(result);
            if (!video_output_options_res) {
                std::cerr << "Error: " << video_output_options_res.error().message << std::endl;
                return 1;
            }

            std::filesystem::path input_path =
                result.count("input") ? std::filesystem::path(result["input"].as<std::string>())
                                      : (args.empty() ? std::filesystem::path{}
                                                      : std::filesystem::path(args.front()));
            auto resolved = resolve_execution_defaults(result, argc, argv, device, false);
            if (!resolved) {
                std::cerr << "Error: " << resolved.error().message << std::endl;
                return 1;
            }

            JobRequest benchmark_request;
            benchmark_request.model_path = resolved->model_path;
            benchmark_request.device = device;
            benchmark_request.params = resolved->params;
            benchmark_request.video_output = *video_output_options_res;
            if (!input_path.empty()) {
                benchmark_request.input_path = input_path;
            }
            if (result.count("alpha-hint")) {
                benchmark_request.hint_path = result["alpha-hint"].as<std::string>();
            }
            if (result.count("output")) {
                benchmark_request.output_path = result["output"].as<std::string>();
            }

            auto report = JobOrchestrator::run_benchmark(benchmark_request);
            if (report.contains("error")) {
                if (use_json) {
                    std::cout << common::safe_json_dump(report, 4) << std::endl;
                } else {
                    std::cerr << "Benchmark error: " << report["error"].get<std::string>()
                              << std::endl;
                }
                return 1;
            }
            if (use_json) {
                std::cout << common::safe_json_dump(report, 4) << std::endl;
            } else {
                std::cout << "--- Benchmark Results ---\n"
                          << "Model: " << benchmark_request.model_path.filename().string() << "\n";
                if (resolved->preset.has_value()) {
                    std::cout << "Preset: " << resolved->preset->name << "\n";
                }
                std::cout << "Requested precision: "
                          << precision_preference_to_string(
                                 benchmark_request.params.precision_preference)
                          << "\n";
                std::cout << "Requested device: " << report["requested_device"].get<std::string>()
                          << "\n"
                          << "Backend: " << report["backend"].get<std::string>() << "\n";
                std::cout << "Mode: " << report["mode"].get<std::string>() << "\n"
                          << "Device: "
                          << (report.contains("device")
                                  ? report["device"].get<std::string>()
                                  : report["requested_device"].get<std::string>())
                          << "\n";
                if (report["mode"] == "synthetic") {
                    std::cout << "Resolution: " << report["resolution"].get<int>() << "x"
                              << report["resolution"].get<int>() << "\n"
                              << "Avg Latency: " << report["avg_latency_ms"].get<double>()
                              << " ms\n"
                              << "Estimated FPS: " << report["fps"].get<double>() << "\n";
                } else {
                    std::cout << "Input: " << report["input"].get<std::string>() << "\n"
                              << "Total Duration: " << report["total_duration_ms"].get<double>()
                              << " ms\n";
                    if (report.contains("processed_units")) {
                        std::cout << "Processed Units: "
                                  << report["processed_units"].get<std::uint64_t>() << "\n"
                                  << "Throughput: "
                                  << report["throughput_units_per_second"].get<double>()
                                  << " units/s\n";
                    }
                    if (report.contains("output_path")) {
                        std::cout << "Output: " << report["output_path"].get<std::string>() << "\n";
                    }
                }
                print_benchmark_artifact_summary(report);
                print_stage_timings(report["stage_timings"]);
            }
            return 0;
        }

        if (cmd == "download") {
            std::string variant =
                result.count("variant") ? result["variant"].as<std::string>() : "int8";
            std::vector<std::string> variants_to_download;

            if (variant == "all") {
                variants_to_download = {"int8", "fp16", "fp32"};
            } else if (variant == "int8" || variant == "fp16" || variant == "fp32") {
                variants_to_download.push_back(variant);
            } else {
                std::cerr << "Error: Invalid variant. Allowed values are 'int8', 'fp16', 'fp32', "
                             "or 'all'."
                          << std::endl;
                return 1;
            }

            std::filesystem::create_directory("models");

            const std::vector<int> available_resolutions = {512, 768, 1024, 1536, 2048};

            for (const auto& v : variants_to_download) {
                for (const int resolution : available_resolutions) {
                    std::string filename =
                        "corridorkey_" + v + "_" + std::to_string(resolution) + ".onnx";
                    std::filesystem::path output_path = std::filesystem::path("models") / filename;
                    std::string url =
                        "https://huggingface.co/corridorkey/models/resolve/main/" + filename;

                    std::cout << "Downloading " << filename << "..." << std::endl;
                    std::ofstream of(output_path, std::ios::binary);

                    cpr::Response r = cpr::Download(
                        of, cpr::Url{url},
                        cpr::ProgressCallback([](size_t downloadTotal, size_t downloadNow, size_t,
                                                 size_t, intptr_t) -> bool {
                            if (downloadTotal > 0) {
                                float p = static_cast<float>(downloadNow) / downloadTotal;
                                int bar_width = 50;
                                auto filled =
                                    static_cast<size_t>(std::clamp(bar_width * p, 0.0F, 50.0F));
                                auto empty = static_cast<size_t>(bar_width) - filled;
                                std::cout << "\r[" << std::string(filled, '=')
                                          << std::string(empty, ' ') << "] " << int(p * 100.0)
                                          << "% " << std::flush;
                            }
                            return true;
                        }));

                    std::cout << std::endl;
                    of.close();

                    if (r.status_code == 200) {
                        std::cout << "Successfully downloaded " << filename << " to models/"
                                  << std::endl;
                    } else {
                        std::cerr << "Failed to download " << filename
                                  << ". HTTP Status: " << r.status_code << std::endl;
                        if (r.status_code == 401 || r.status_code == 404) {
                            std::cerr << "Note: The HuggingFace repository may be private or not "
                                         "yet created."
                                      << std::endl;
                        }
                        std::filesystem::remove(output_path);
                    }
                }
            }
            return 0;
        }

        if (cmd == "process") {
            auto resolved_paths = cli::resolve_process_paths(
                result.count("input") ? std::optional<std::filesystem::path>{std::filesystem::path(
                                            result["input"].as<std::string>())}
                                      : std::nullopt,
                result.count("output") ? std::optional<std::filesystem::path>{std::filesystem::path(
                                             result["output"].as<std::string>())}
                                       : std::nullopt,
                args);
            if (!resolved_paths) {
                std::cout << "Error: " << resolved_paths.error().message << std::endl;
                return 1;
            }

            std::filesystem::path input_path = resolved_paths->input_path;
            std::filesystem::path output_path = resolved_paths->output_path;

            if (input_path.empty()) {
                std::cout << "Error: 'process' requires an input path." << std::endl;
                return 1;
            }

            auto devices = list_devices();
            std::string device_str = result["device"].as<std::string>();
            DeviceInfo device = cli::select_device(devices, device_str);
            auto video_output_options_res = resolve_video_output_options(result);
            if (!video_output_options_res) {
                std::cerr << "Error: " << video_output_options_res.error().message << std::endl;
                return 1;
            }
            auto resolved = resolve_execution_defaults(result, argc, argv, device, true);
            if (!resolved) {
                std::cerr << "Error: " << resolved.error().message << std::endl;
                return 1;
            }
            if (output_path.empty()) {
                auto default_output =
                    default_output_path_for_input(input_path, *video_output_options_res);
                if (!default_output) {
                    std::cerr << "Error: " << default_output.error().message << std::endl;
                    return 1;
                }
                output_path = *default_output;
                resolved->default_output_selected = true;
            }

            JobRequest req;
            req.input_path = input_path;
            req.hint_path =
                result.count("alpha-hint") ? result["alpha-hint"].as<std::string>() : "";
            req.output_path = output_path;
            req.model_path = resolved->model_path;
            req.params = resolved->params;
            req.video_output = *video_output_options_res;
            req.device = device;

            if (!use_json) {
                std::cout << "Processing setup:\n"
                          << " - Input: " << req.input_path.string() << "\n"
                          << " - Output: " << req.output_path.string();
                if (resolved->default_output_selected) {
                    std::cout << " [auto]";
                }
                std::cout << "\n";
                if (is_video_path(req.input_path)) {
                    std::cout << " - Video encode: "
                              << (req.video_output.mode == VideoOutputMode::Lossless ? "lossless"
                                                                                     : "balanced")
                              << "\n";
                }
                if (resolved->preset.has_value()) {
                    std::cout << " - Preset: " << resolved->preset->name << "\n";
                }
                std::cout << " - Precision: "
                          << precision_preference_to_string(req.params.precision_preference)
                          << "\n";
                std::cout << " - Model: " << req.model_path.filename().string();
                if (resolved->default_model_selected) {
                    std::cout << " [auto]";
                }
                const std::string effective_precision = artifact_precision_label(req.model_path);
                if (effective_precision != "auto") {
                    std::cout << " [" << effective_precision << "]";
                }
                std::cout << "\n"
                          << " - Requested device: " << req.device.name << " [" << device_str
                          << "]\n";
                if (auto model_resolution = packaged_model_resolution(req.model_path);
                    model_resolution.has_value()) {
                    std::cout << " - Effective resolution: " << *model_resolution << "\n";
                }
            }

            auto progress = [](float p, const std::string& status) -> bool {
                {
                    int bar_width = 50;
                    auto filled = static_cast<size_t>(std::clamp(bar_width * p, 0.0F, 50.0F));
                    auto empty = static_cast<size_t>(bar_width) - filled;
                    std::cout << "\r[" << std::string(filled, '=') << std::string(empty, ' ')
                              << "] " << int(p * 100.0) << "% " << status << std::flush;
                }
                return true;
            };

            JobEventCallback events = nullptr;
            if (use_json) {
                events = [](const JobEvent& event) -> bool {
                    std::cout << common::safe_json_dump(event_to_json(event)) << std::endl;
                    return true;
                };
            } else {
                events = [](const JobEvent& event) -> bool {
                    if (event.type == JobEventType::BackendSelected) {
                        std::cout << "Selected backend: " << backend_to_string_local(event.backend);
                        if (!event.message.empty()) {
                            std::cout << " (" << event.message << ")";
                        }
                        std::cout << std::endl;
                    }
                    if (event.type == JobEventType::Warning) {
                        std::cerr << "Warning: " << event.message << std::endl;
                        if (event.fallback.has_value()) {
                            std::cerr << "Fallback reason: " << event.fallback->reason << std::endl;
                        }
                    }
                    return true;
                };
            }

            auto process_res = JobOrchestrator::run(req, use_json ? nullptr : progress, events);
            if (!use_json) std::cout << std::endl;

            if (!process_res) {
                if (!use_json) {
                    std::cerr << "Error: " << process_res.error().message << std::endl;
                }
                return 1;
            }

            if (!use_json) {
                std::cout << "Done!" << std::endl;
            }
            return 0;
        }

        std::cout << "Unknown command: " << cmd << std::endl;
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return 1;
    }
}
