#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey::core {

class MlxSession {
   public:
    static Result<std::unique_ptr<MlxSession>> create(const std::filesystem::path& model_path,
                                                      const DeviceInfo& device);

    ~MlxSession();

    MlxSession(const MlxSession&) = delete;
    MlxSession& operator=(const MlxSession&) = delete;
    MlxSession(MlxSession&&) noexcept;
    MlxSession& operator=(MlxSession&&) noexcept;

    [[nodiscard]] Result<FrameResult> infer(const Image& rgb, const Image& alpha_hint,
                                            bool output_alpha_only = false,
                                            UpscaleMethod upscale_method = UpscaleMethod::Lanczos4,
                                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] Result<FrameResult> infer_tile(const Image& rgb_tile, const Image& hint_tile,
                                                 bool output_alpha_only = false,
                                                 StageTimingCallback on_stage = nullptr);

    [[nodiscard]] int model_resolution() const;

   private:
    MlxSession();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace corridorkey::core
