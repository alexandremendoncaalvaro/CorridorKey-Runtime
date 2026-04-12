#pragma once

#include <array>
#include <corridorkey/types.hpp>
#include <memory>

namespace corridorkey::core {

struct GpuPrepState;

class GpuInputPrep {
   public:
    GpuInputPrep();
    ~GpuInputPrep();

    GpuInputPrep(const GpuInputPrep&) = delete;
    GpuInputPrep& operator=(const GpuInputPrep&) = delete;
    GpuInputPrep(GpuInputPrep&&) noexcept;
    GpuInputPrep& operator=(GpuInputPrep&&) noexcept;

    [[nodiscard]] bool available() const;

    [[nodiscard]] Result<void> prepare_inputs(
        Image rgb, Image hint,
        float* planar_dst,
        int model_width, int model_height,
        const std::array<float, 3>& mean,
        const std::array<float, 3>& inv_stddev);

   private:
    std::unique_ptr<GpuPrepState> m_state;
};

}  // namespace corridorkey::core
