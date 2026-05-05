#pragma once

#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>

#include "app/ofx_runtime_protocol.hpp"

namespace corridorkey::ofx {

enum class OfxRuntimeFamily : std::uint8_t {
    Other,
    OrtTensorRt,
    TorchTrt,
};

inline bool ofx_artifact_is_torchscript(const std::filesystem::path& artifact_path) {
    return artifact_path.extension() == ".ts";
}

inline bool ofx_artifact_is_onnx(const std::filesystem::path& artifact_path) {
    return artifact_path.extension() == ".onnx";
}

inline OfxRuntimeFamily ofx_runtime_family_for_backend_and_artifact(
    Backend backend, const std::filesystem::path& artifact_path) {
    if (backend == Backend::TorchTRT || ofx_artifact_is_torchscript(artifact_path)) {
        return OfxRuntimeFamily::TorchTrt;
    }
    if (backend == Backend::TensorRT && ofx_artifact_is_onnx(artifact_path)) {
        return OfxRuntimeFamily::OrtTensorRt;
    }
    return OfxRuntimeFamily::Other;
}

inline OfxRuntimeFamily ofx_runtime_family_for_prepare_request(
    const app::OfxRuntimePrepareSessionRequest& request) {
    return ofx_runtime_family_for_backend_and_artifact(request.requested_device.backend,
                                                       request.model_path);
}

inline bool should_restart_for_ofx_runtime_family_switch(OfxRuntimeFamily current_family,
                                                         OfxRuntimeFamily next_family) {
    if (current_family == next_family) {
        return false;
    }
    const bool current_is_windows_rtx = current_family == OfxRuntimeFamily::OrtTensorRt ||
                                        current_family == OfxRuntimeFamily::TorchTrt;
    const bool next_is_windows_rtx = next_family == OfxRuntimeFamily::OrtTensorRt ||
                                     next_family == OfxRuntimeFamily::TorchTrt;
    return current_is_windows_rtx && next_is_windows_rtx;
}

inline const char* ofx_runtime_family_label(OfxRuntimeFamily family) {
    switch (family) {
        case OfxRuntimeFamily::OrtTensorRt:
            return "ort_tensorrt";
        case OfxRuntimeFamily::TorchTrt:
            return "torchtrt";
        case OfxRuntimeFamily::Other:
        default:
            return "other";
    }
}

}  // namespace corridorkey::ofx
