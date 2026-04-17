#pragma once

#include <corridorkey/types.hpp>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
// psapi.h depends on Windows types/macros from windows.h.
#include <windows.h>
#include <psapi.h>
// clang-format on
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#endif

namespace corridorkey::common {

struct SystemMetrics {
    double cpu_usage_percent = 0.0;
    std::uint64_t ram_usage_mb = 0;
    std::uint64_t vram_usage_mb = 0;
    // Peak resident set size since the process started, in megabytes.
    // 0 when the platform does not expose a reliable peak metric.
    std::uint64_t peak_ram_mb = 0;
    // System-wide wired (non-pageable) memory at the moment of sampling, in megabytes.
    // 0 when the platform does not expose the figure without extra privileges.
    std::uint64_t system_wired_mb = 0;
};

inline SystemMetrics get_current_metrics() {
    SystemMetrics metrics;

#if defined(_WIN32)
    // RAM Usage
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        metrics.ram_usage_mb = pmc.WorkingSetSize / (1024 * 1024);
        metrics.peak_ram_mb = pmc.PeakWorkingSetSize / (1024 * 1024);
    }

    // CPU Usage (Simplificado para o processo atual)
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        // Nota: Uma implementação real de CPU exige delta entre duas leituras.
        // Para evitar bloqueio, reportaremos 0.0 ou usaremos um worker se necessário.
        metrics.cpu_usage_percent = 0.0;
    }
#elif defined(__APPLE__)
    // Current resident size via the basic task info.
    struct mach_task_basic_info basic_info;
    mach_msg_type_number_t basic_count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&basic_info, &basic_count) ==
        KERN_SUCCESS) {
        metrics.ram_usage_mb = basic_info.resident_size / (1024 * 1024);
        metrics.peak_ram_mb = basic_info.resident_size_max / (1024 * 1024);
    }

    // System-wide wired memory via mach host statistics. Unified memory on Apple Silicon
    // means the wired figure correlates with kernel- and IO-pinned allocations, which is a
    // useful signal when diagnosing pressure during large MLX loads.
    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
    if (page_size > 0 && host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                           (host_info64_t)&vm_stats, &vm_count) == KERN_SUCCESS) {
        metrics.system_wired_mb =
            (static_cast<std::uint64_t>(vm_stats.wire_count) * page_size) / (1024 * 1024);
    }
#endif

    return metrics;
}

}  // namespace corridorkey::common
