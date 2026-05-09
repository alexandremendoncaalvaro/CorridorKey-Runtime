#pragma once

#include <array>
#include <corridorkey/types.hpp>
#include <memory>

namespace corridorkey::core {

struct GpuPrepState;

struct GpuPreparedInput {
    void* planar_device = nullptr;
    void* ready_start_event = nullptr;
    void* ready_event = nullptr;
    bool ready_event_on_current_stream = false;
    void* source_rgb_device = nullptr;
    int source_width = 0;
    int source_height = 0;
    int source_channels = 0;
    int width = 0;
    int height = 0;
};

class GpuInputPrep {
   public:
    GpuInputPrep();
    ~GpuInputPrep();

    GpuInputPrep(const GpuInputPrep&) = delete;
    GpuInputPrep& operator=(const GpuInputPrep&) = delete;
    GpuInputPrep(GpuInputPrep&&) noexcept;
    GpuInputPrep& operator=(GpuInputPrep&&) noexcept;

    [[nodiscard]] bool available() const;

    [[nodiscard]] Result<void> prepare_inputs(Image rgb, Image hint, float* planar_dst,
                                              int model_width, int model_height,
                                              const std::array<float, 3>& mean,
                                              const std::array<float, 3>& inv_stddev,
                                              StageTimingCallback on_stage = nullptr);

    [[nodiscard]] Result<GpuPreparedInput> prepare_inputs_device(
        Image rgb, Image hint, int model_width, int model_height, const std::array<float, 3>& mean,
        const std::array<float, 3>& inv_stddev, StageTimingCallback on_stage = nullptr);

    [[nodiscard]] Result<GpuPreparedInput> prepare_inputs_device_on_stream(
        Image rgb, Image hint, int model_width, int model_height, const std::array<float, 3>& mean,
        const std::array<float, 3>& inv_stddev, void* cuda_stream,
        StageTimingCallback on_stage = nullptr);

   private:
    std::unique_ptr<GpuPrepState> m_state;
};

}  // namespace corridorkey::core
