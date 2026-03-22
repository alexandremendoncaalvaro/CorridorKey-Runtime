#pragma once

#include <string_view>

namespace corridorkey::detail {

/**
 * @brief ONNX Runtime Execution Provider names.
 * Centralized to avoid case-sensitivity issues and "magic strings".
 */
namespace providers {
    static constexpr std::string_view CPU = "CPUExecutionProvider";
    static constexpr std::string_view CUDA = "CUDAExecutionProvider";
    static constexpr std::string_view TENSORRT = "TensorrtExecutionProvider";
    static constexpr std::string_view COREML = "CoreMLExecutionProvider";
    static constexpr std::string_view DIRECTML = "DmlExecutionProvider";
    static constexpr std::string_view WINML = "WinMLExecutionProvider";
    static constexpr std::string_view OPENVINO = "OpenVINOExecutionProvider";
    static constexpr std::string_view SNPE = "SNPEExecutionProvider";
    static constexpr std::string_view QNN = "QNNExecutionProvider";
}

/**
 * @brief Common ONNX Runtime Session Option keys.
 */
namespace session_options {
    static constexpr std::string_view DEVICE_ID = "device_id";
}

} // namespace corridorkey::detail
