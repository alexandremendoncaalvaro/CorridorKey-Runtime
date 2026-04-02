#pragma once

#include <chrono>

namespace corridorkey::common {

inline constexpr int kDefaultOfxRenderTimeoutSeconds = 60;
inline constexpr int kDefaultOfxPrepareTimeoutSeconds = 300;
inline constexpr auto kDefaultOfxIdleTimeout = std::chrono::minutes(5);
inline constexpr int kDefaultOfxRequestTimeoutMs = kDefaultOfxRenderTimeoutSeconds * 1000;
inline constexpr int kDefaultOfxPrepareTimeoutMs = kDefaultOfxPrepareTimeoutSeconds * 1000;
inline constexpr int kDefaultOfxIdleTimeoutMs = static_cast<int>(
    std::chrono::duration_cast<std::chrono::milliseconds>(kDefaultOfxIdleTimeout).count());

}  // namespace corridorkey::common
