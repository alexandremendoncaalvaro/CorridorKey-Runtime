#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>

namespace corridorkey::core {

bool mlx_probe_available();
Result<void> probe_mlx_function(const std::filesystem::path& function_path);

}  // namespace corridorkey::core
