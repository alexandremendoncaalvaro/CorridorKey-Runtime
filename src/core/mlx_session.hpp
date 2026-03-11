#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

namespace corridorkey::core {

class MlxSession {
   public:
    static Result<std::unique_ptr<MlxSession>> create(const std::filesystem::path& model_path);

    ~MlxSession();

    MlxSession(const MlxSession&) = delete;
    MlxSession& operator=(const MlxSession&) = delete;
    MlxSession(MlxSession&&) noexcept;
    MlxSession& operator=(MlxSession&&) noexcept;

    [[nodiscard]] Result<FrameResult> infer(const Image& rgb, const Image& alpha_hint,
                                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] int model_resolution() const;

   private:
    MlxSession();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace corridorkey::core
