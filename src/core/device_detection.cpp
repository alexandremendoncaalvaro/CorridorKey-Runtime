#include <corridorkey/engine.hpp>
#include <iostream>

namespace corridorkey {

DeviceInfo auto_detect() {
    // TODO: Implement real hardware detection (check for CoreML, TensorRT, etc.)
    return { "CPU (Fallback)", 16384, Backend::CPU };
}

std::vector<DeviceInfo> list_devices() {
    std::vector<DeviceInfo> devices;
    devices.push_back(auto_detect());
    return devices;
}

} // namespace corridorkey
