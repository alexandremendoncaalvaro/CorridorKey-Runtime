#pragma once

#include <chrono>

namespace corridorkey::common {

inline constexpr int kDefaultHostPluginRenderTimeoutSeconds = 60;
inline constexpr int kDefaultHostPluginPrepareTimeoutSeconds = 300;
inline constexpr auto kDefaultHostPluginIdleTimeout = std::chrono::minutes(5);
inline constexpr int kDefaultHostPluginRequestTimeoutMs =
    kDefaultHostPluginRenderTimeoutSeconds * 1000;
inline constexpr int kDefaultHostPluginPrepareTimeoutMs =
    kDefaultHostPluginPrepareTimeoutSeconds * 1000;
inline constexpr int kDefaultHostPluginIdleTimeoutMs = static_cast<int>(
    std::chrono::duration_cast<std::chrono::milliseconds>(kDefaultHostPluginIdleTimeout).count());

}  // namespace corridorkey::common
