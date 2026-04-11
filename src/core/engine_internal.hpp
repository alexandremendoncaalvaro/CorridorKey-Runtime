#pragma once

#include <corridorkey/engine.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey::core {

class OrtProcessContext;

struct EngineFactory {
    static Result<std::unique_ptr<Engine>> create_with_ort_process_context(
        const std::filesystem::path& model_path, DeviceInfo device,
        std::shared_ptr<OrtProcessContext> ort_process_context,
        StageTimingCallback on_stage = nullptr, EngineCreateOptions options = {});
};

}  // namespace corridorkey::core
