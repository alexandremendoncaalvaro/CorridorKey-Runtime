#pragma once

#include <corridorkey/types.hpp>
#include <memory>

namespace corridorkey::core {

struct GpuResizeState;

class GpuResizer {
   public:
    GpuResizer();
    ~GpuResizer();

    GpuResizer(const GpuResizer&) = delete;
    GpuResizer& operator=(const GpuResizer&) = delete;
    GpuResizer(GpuResizer&&) noexcept;
    GpuResizer& operator=(GpuResizer&&) noexcept;

    [[nodiscard]] bool available() const;

    [[nodiscard]] Result<void> resize_planar_outputs(const float* src_alpha, const float* src_fg,
                                                     int src_width, int src_height, Image dst_alpha,
                                                     Image dst_fg);

   private:
    std::unique_ptr<GpuResizeState> m_state;
};

}  // namespace corridorkey::core
