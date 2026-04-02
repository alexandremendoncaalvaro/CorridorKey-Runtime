#include "model_compiler.hpp"

namespace corridorkey::app {

Result<std::filesystem::path> compile_tensorrt_rtx_context_model(
    const std::filesystem::path& input_model_path, const std::filesystem::path& output_model_path,
    const DeviceInfo& device) {
    (void)input_model_path;
    (void)output_model_path;
    (void)device;
    return Unexpected(Error{ErrorCode::HardwareNotSupported,
                            "ONNX Runtime context compilation has been removed. "
                            "Windows inference uses Torch-TensorRT exclusively."});
}

}  // namespace corridorkey::app
