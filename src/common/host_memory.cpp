#include "host_memory.hpp"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <unistd.h>
#endif

namespace corridorkey::common {

HostMemoryStats query_host_memory_stats() {
    HostMemoryStats out;
#ifdef __APPLE__
    vm_statistics64_data_t info{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&info),
                          &count) != KERN_SUCCESS) {
        return out;
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return out;
    }
    const std::size_t bytes_per_page = static_cast<std::size_t>(page_size);
    out.free_bytes = static_cast<std::size_t>(info.free_count) * bytes_per_page;
    out.compressor_bytes = static_cast<std::size_t>(info.compressor_page_count) * bytes_per_page;
#endif
    return out;
}

}  // namespace corridorkey::common
