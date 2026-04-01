#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey::core {

class TorchTrtSession {
   public:
    static Result<std::unique_ptr<TorchTrtSession>> create(const std::filesystem::path& model_path,
                                                           DeviceInfo device);

    ~TorchTrtSession();
    TorchTrtSession(TorchTrtSession&&) noexcept;
    TorchTrtSession& operator=(TorchTrtSession&&) noexcept;

    TorchTrtSession(const TorchTrtSession&) = delete;
    TorchTrtSession& operator=(const TorchTrtSession&) = delete;

    [[nodiscard]] Result<FrameResult> infer(const Image& rgb, const Image& alpha_hint,
                                            const InferenceParams& params,
                                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] int model_resolution() const;
    [[nodiscard]] DeviceInfo device() const;

   private:
    class Impl;

    explicit TorchTrtSession(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] bool torch_tensorrt_runtime_available();

}  // namespace corridorkey::core
