#pragma once

#include <corridorkey/types.hpp>

#include <filesystem>
#include <string>

namespace corridorkey::app::detail {

inline std::string canonical_ofx_artifact_name(const std::filesystem::path& model_path) {
    return model_path.filename().string();
}

inline bool should_destroy_zero_ref_session(Backend backend) {
    return backend == Backend::TensorRT;
}

}  // namespace corridorkey::app::detail
