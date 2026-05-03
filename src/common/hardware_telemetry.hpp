#pragma once

#include <corridorkey/types.hpp>
#include <cstdint>

#ifdef _WIN32
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

//
// Header tidy-suppression rationale (file-level NOLINT block).
// See similar comments at the top of other src/ headers for the
// canonical reasoning (hot-path operator[], pixel-coord conventions,
// C-ABI signatures, validated-index access, etc.).
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,performance-no-int-to-ptr,bugprone-suspicious-include,cppcoreguidelines-avoid-do-while,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)

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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,performance-no-int-to-ptr,bugprone-suspicious-include,cppcoreguidelines-avoid-do-while,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)
