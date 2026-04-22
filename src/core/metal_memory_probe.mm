#include "metal_memory_probe.hpp"

#if defined(__APPLE__)
#import <Metal/Metal.h>
#endif

namespace corridorkey::core::metal_memory {

std::size_t recommended_max_working_set_bytes() {
#if defined(__APPLE__)
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            return 0;
        }
        return static_cast<std::size_t>([device recommendedMaxWorkingSetSize]);
    }
#else
    return 0;
#endif
}

}  // namespace corridorkey::core::metal_memory
