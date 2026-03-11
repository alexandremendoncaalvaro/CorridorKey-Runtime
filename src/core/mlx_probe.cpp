#include "mlx_probe.hpp"

#include <filesystem>
#include <string>

#if CORRIDORKEY_WITH_MLX
#include <mlx/mlx.h>
#endif

namespace corridorkey::core {

bool mlx_probe_available() {
#if CORRIDORKEY_WITH_MLX
    return true;
#else
    return false;
#endif
}

Result<void> probe_mlx_function(const std::filesystem::path& function_path) {
    if (!std::filesystem::exists(function_path)) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "MLX function artifact not found: " + function_path.string()}};
    }

#if !CORRIDORKEY_WITH_MLX
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX probe support is not linked in this build. Install MLX and reconfigure CMake."}};
#else
    try {
        auto imported = mlx::core::import_function(function_path.string());
        (void)imported;
        return {};
    } catch (const std::exception& error) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "Failed to import MLX function: " + std::string(error.what())}};
    }
#endif
}

}  // namespace corridorkey::core
