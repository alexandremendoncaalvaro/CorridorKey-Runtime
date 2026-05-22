#pragma once

#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>

#include "app/host_plugin_runtime_protocol.hpp"

namespace corridorkey::app {

enum class HostPluginRuntimeFamily : std::uint8_t {
    Other,
    OrtTensorRt,
    TorchTrt,
};

inline bool host_plugin_artifact_is_torchscript(const std::filesystem::path& artifact_path) {
    return artifact_path.extension() == ".ts";
}

inline bool host_plugin_artifact_is_onnx(const std::filesystem::path& artifact_path) {
    return artifact_path.extension() == ".onnx";
}

inline HostPluginRuntimeFamily host_plugin_runtime_family_for_backend_and_artifact(
    Backend backend, const std::filesystem::path& artifact_path) {
    if (backend == Backend::TorchTRT || host_plugin_artifact_is_torchscript(artifact_path)) {
        return HostPluginRuntimeFamily::TorchTrt;
    }
    if (backend == Backend::TensorRT && host_plugin_artifact_is_onnx(artifact_path)) {
        return HostPluginRuntimeFamily::OrtTensorRt;
    }
    return HostPluginRuntimeFamily::Other;
}

inline HostPluginRuntimeFamily host_plugin_runtime_family_for_prepare_request(
    const HostPluginRuntimePrepareSessionRequest& request) {
    return host_plugin_runtime_family_for_backend_and_artifact(request.requested_device.backend,
                                                               request.model_path);
}

inline bool should_restart_for_host_plugin_runtime_family_switch(
    HostPluginRuntimeFamily current_family, HostPluginRuntimeFamily next_family) {
    if (current_family == next_family) {
        return false;
    }
    const bool current_is_windows_rtx = current_family == HostPluginRuntimeFamily::OrtTensorRt ||
                                        current_family == HostPluginRuntimeFamily::TorchTrt;
    const bool next_is_windows_rtx = next_family == HostPluginRuntimeFamily::OrtTensorRt ||
                                     next_family == HostPluginRuntimeFamily::TorchTrt;
    return current_is_windows_rtx && next_is_windows_rtx;
}

inline const char* host_plugin_runtime_family_label(HostPluginRuntimeFamily family) {
    switch (family) {
        case HostPluginRuntimeFamily::OrtTensorRt:
            return "ort_tensorrt";
        case HostPluginRuntimeFamily::TorchTrt:
            return "torchtrt";
        case HostPluginRuntimeFamily::Other:
        default:
            return "other";
    }
}

}  // namespace corridorkey::app
