#pragma once

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>
#include <filesystem>

namespace corridorkey::app {

CORRIDORKEY_API Result<std::filesystem::path> compile_tensorrt_rtx_context_model(
    const std::filesystem::path& input_model_path, const std::filesystem::path& output_model_path,
    const DeviceInfo& device);

}  // namespace corridorkey::app
