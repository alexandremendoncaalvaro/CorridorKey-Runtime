#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey::core {

// Wraps a Torch-TensorRT compiled .ts engine for in-process forward.
// Sister to MlxSession - same wrapping pattern used by InferenceSession,
// gated on Backend::TorchTRT. Built only when CORRIDORKEY_HAS_TORCHTRT is
// defined (vendor/torchtrt-windows/ staged via prepare-torchtrt).
//
// Strategy C, Sprint 1: this session is the runtime entrypoint for the
// blue model pack on Windows RTX. Engines are compiled with
// hardware_compatible=True so the same .ts runs on RTX 30/40/50.
class TorchTrtSession {
   public:
    static Result<std::unique_ptr<TorchTrtSession>> create(const std::filesystem::path& ts_path,
                                                           const DeviceInfo& device,
                                                           StageTimingCallback on_stage = nullptr);

    ~TorchTrtSession();

    TorchTrtSession(const TorchTrtSession&) = delete;
    TorchTrtSession& operator=(const TorchTrtSession&) = delete;
    TorchTrtSession(TorchTrtSession&&) noexcept;
    TorchTrtSession& operator=(TorchTrtSession&&) noexcept;

    [[nodiscard]] Result<FrameResult> infer(const Image& rgb, const Image& alpha_hint,
                                            bool output_alpha_only = false,
                                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] int model_resolution() const;

   private:
    TorchTrtSession();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace corridorkey::core
