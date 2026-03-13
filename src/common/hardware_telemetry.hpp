#pragma once

#include <corridorkey/types.hpp>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace corridorkey::common {

struct SystemMetrics {
    double cpu_usage_percent = 0.0;
    std::uint64_t ram_usage_mb = 0;
    std::uint64_t vram_usage_mb = 0;
};

inline SystemMetrics get_current_metrics() {
    SystemMetrics metrics;

#if defined(_WIN32)
    // RAM Usage
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        metrics.ram_usage_mb = pmc.WorkingSetSize / (1024 * 1024);
    }

    // CPU Usage (Simplificado para o processo atual)
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        // Nota: Uma implementação real de CPU exige delta entre duas leituras.
        // Para evitar bloqueio, reportaremos 0.0 ou usaremos um worker se necessário.
        metrics.cpu_usage_percent = 0.0; 
    }
#elif defined(__APPLE__)
    // RAM Usage
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        metrics.ram_usage_mb = info.resident_size / (1024 * 1024);
    }
#endif

    return metrics;
}

} // namespace corridorkey::common
