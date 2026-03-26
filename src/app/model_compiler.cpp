#include "model_compiler.hpp"

#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#else
#error "ONNX Runtime C++ headers not found"
#endif

#if __has_include(<onnxruntime/core/session/onnxruntime_session_options_config_keys.h>)
#include <onnxruntime/core/session/onnxruntime_session_options_config_keys.h>
#define CORRIDORKEY_HAS_ORT_SESSION_CONFIG_KEYS 1
#endif

#include <system_error>
#include <unordered_map>

#include "../core/windows_rtx_probe.hpp"

namespace corridorkey::app {

namespace {

#if defined(_WIN32) && defined(ORT_API_VERSION) && ORT_API_VERSION >= 22
constexpr const char* kTensorRtRtxExecutionProvider = "NvTensorRTRTXExecutionProvider";

constexpr const char* kDeviceId = "device_id";
constexpr const char* kEnableValue = "1";

#if defined(CORRIDORKEY_HAS_ORT_SESSION_CONFIG_KEYS)
const char* const kEpContextEnable = kOrtSessionOptionEpContextEnable;
const char* const kEpContextFilePath = kOrtSessionOptionEpContextFilePath;
const char* const kEpContextEmbedMode = kOrtSessionOptionEpContextEmbedMode;
#else
constexpr const char* kEpContextEnable = "ep.context_enable";
constexpr const char* kEpContextFilePath = "ep.context_file_path";
constexpr const char* kEpContextEmbedMode = "ep.context_embed_mode";
#endif

constexpr const char* kMaxWorkspaceSize = "nv_max_workspace_size";

int extract_model_resolution(const std::filesystem::path& model_path) {
    auto filename = model_path.filename().string();
    for (int res : {2048, 1536, 1024, 768, 512}) {
        if (filename.find("_" + std::to_string(res)) != std::string::npos) {
            return res;
        }
    }
    return 1024;
}

void append_tensorrt_rtx_provider(Ort::SessionOptions& session_options,
                                  const std::filesystem::path& model_path) {
    int model_res = extract_model_resolution(model_path);

    constexpr const char* kWorkspace2GB = "2147483648";
    constexpr const char* kWorkspace4GB = "4294967296";
    constexpr const char* kWorkspace8GB = "8589934592";
    const char* workspace_size = kWorkspace2GB;
    if (model_res >= 2048) {
        workspace_size = kWorkspace8GB;
    } else if (model_res >= 1536) {
        workspace_size = kWorkspace4GB;
    }

    std::unordered_map<std::string, std::string> provider_options = {
        {kDeviceId, "0"},
        {kMaxWorkspaceSize, workspace_size},
    };

    session_options.AppendExecutionProvider(kTensorRtRtxExecutionProvider, provider_options);
}

Result<std::filesystem::path> compile_with_compile_api(
    const std::filesystem::path& input_model_path, const std::filesystem::path& output_model_path) {
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "CorridorKeyModelCompiler");
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
        append_tensorrt_rtx_provider(session_options, input_model_path);

        Ort::ModelCompilationOptions compile_options(env, session_options);
#if defined(_WIN32)
        compile_options.SetInputModelPath(input_model_path.wstring().c_str());
        compile_options.SetOutputModelPath(output_model_path.wstring().c_str());
#else
        compile_options.SetInputModelPath(input_model_path.c_str());
        compile_options.SetOutputModelPath(output_model_path.c_str());
#endif
        compile_options.SetEpContextEmbedMode(true);
        compile_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        auto status = Ort::CompileModel(env, compile_options);
        if (!status.IsOK()) {
            return Unexpected(
                Error{ErrorCode::InferenceFailed,
                      "TensorRT RTX compile API failed: " + std::string(status.GetErrorMessage())});
        }

        return output_model_path;
    } catch (const Ort::Exception& error) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "TensorRT RTX compile API failed: " + std::string(error.what())});
    } catch (const std::exception& error) {
        return Unexpected(Error{ErrorCode::InferenceFailed,
                                "TensorRT RTX compile API failed: " + std::string(error.what())});
    }
}

Result<std::filesystem::path> compile_with_ep_context_dump(
    const std::filesystem::path& input_model_path, const std::filesystem::path& output_model_path) {
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "CorridorKeyModelCompiler");
        Ort::SessionOptions session_options;
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
        append_tensorrt_rtx_provider(session_options, input_model_path);
        session_options.AddConfigEntry(kEpContextEnable, kEnableValue);
        session_options.AddConfigEntry(kEpContextFilePath, output_model_path.string().c_str());
        session_options.AddConfigEntry(kEpContextEmbedMode, kEnableValue);

#if defined(_WIN32)
        Ort::Session session(env, input_model_path.wstring().c_str(), session_options);
#else
        Ort::Session session(env, input_model_path.c_str(), session_options);
#endif
        (void)session;

        if (!std::filesystem::exists(output_model_path)) {
            return Unexpected(Error{
                ErrorCode::InferenceFailed,
                "TensorRT RTX EP context generation did not emit the requested output model."});
        }

        return output_model_path;
    } catch (const Ort::Exception& error) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed,
                  "TensorRT RTX EP context generation failed: " + std::string(error.what())});
    } catch (const std::exception& error) {
        return Unexpected(
            Error{ErrorCode::InferenceFailed,
                  "TensorRT RTX EP context generation failed: " + std::string(error.what())});
    }
}
#endif

}  // namespace

Result<std::filesystem::path> compile_tensorrt_rtx_context_model(
    const std::filesystem::path& input_model_path, const std::filesystem::path& output_model_path,
    const DeviceInfo& device) {
#if !defined(ORT_API_VERSION) || ORT_API_VERSION < 22
    (void)input_model_path;
    (void)output_model_path;
    (void)device;
    return Unexpected(Error{ErrorCode::HardwareNotSupported,
                            "This ONNX Runtime build does not expose the model compilation API."});
#elif !defined(_WIN32)
    (void)input_model_path;
    (void)output_model_path;
    (void)device;
    return Unexpected(Error{ErrorCode::HardwareNotSupported,
                            "TensorRT RTX context compilation is only supported on Windows."});
#else
    if (device.backend != Backend::TensorRT) {
        return Unexpected(Error{ErrorCode::InvalidParameters,
                                "TensorRT RTX compilation requires --device tensorrt."});
    }
    if (!std::filesystem::exists(input_model_path)) {
        return Unexpected(Error{ErrorCode::ModelLoadFailed,
                                "Model file not found: " + input_model_path.string()});
    }
    if (!core::tensorrt_rtx_provider_available()) {
        return Unexpected(Error{ErrorCode::HardwareNotSupported,
                                "TensorRT RTX provider is not available in this build."});
    }

    std::error_code error;
    if (!output_model_path.parent_path().empty()) {
        std::filesystem::create_directories(output_model_path.parent_path(), error);
        if (error) {
            return Unexpected(Error{ErrorCode::IoError, "Failed to prepare output directory: " +
                                                            output_model_path.string()});
        }
    }

    std::filesystem::remove(output_model_path, error);

    auto compile_result = compile_with_compile_api(input_model_path, output_model_path);
    if (compile_result) {
        return compile_result;
    }

    auto ep_context_result = compile_with_ep_context_dump(input_model_path, output_model_path);
    if (ep_context_result) {
        return ep_context_result;
    }

    return Unexpected(Error{ErrorCode::InferenceFailed,
                            compile_result.error().message +
                                " Falling back to EP context generation also failed: " +
                                ep_context_result.error().message});
#endif
}

}  // namespace corridorkey::app
