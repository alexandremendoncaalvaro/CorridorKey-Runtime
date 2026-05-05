#include "ort_process_context.hpp"

namespace corridorkey::core {

namespace {

constexpr const char* kOrtLogId = "CorridorKey";

}  // namespace

OrtProcessContext::OrtProcessContext() : m_cpu_arena_cfg(0, -1, -1, -1) {}

Ort::Env& OrtProcessContext::acquire_env(OrtLoggingLevel log_severity) {
    std::scoped_lock lock(m_mutex);
    ensure_initialized(log_severity);
    ensure_shared_cpu_allocator();
    return m_env;
}

void OrtProcessContext::ensure_initialized(OrtLoggingLevel log_severity) {
    if (!m_initialized) {
        // ORT's shared thread-pool guidance expects a process-wide env created with global
        // thread pools, leaving the pool sizes at 0 so the runtime chooses its validated
        // defaults for the host.
        m_threading_options.SetGlobalIntraOpNumThreads(0);
        m_threading_options.SetGlobalInterOpNumThreads(0);
        m_env = Ort::Env(m_threading_options, log_severity, kOrtLogId);
        m_log_severity = log_severity;
        m_initialized = true;
        return;
    }

    if (log_severity != m_log_severity) {
        m_env.UpdateEnvWithCustomLogLevel(log_severity);
        m_log_severity = log_severity;
    }
}

void OrtProcessContext::ensure_shared_cpu_allocator() {
    if (m_cpu_allocator_registered) {
        return;
    }

    // Register the CPU arena allocator once at the env level so multiple sessions can reuse it
    // when they opt into `session.use_env_allocators=1`.
    m_cpu_memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    m_env.CreateAndRegisterAllocator(m_cpu_memory_info, m_cpu_arena_cfg);
    m_cpu_allocator_registered = true;
}

}  // namespace corridorkey::core
