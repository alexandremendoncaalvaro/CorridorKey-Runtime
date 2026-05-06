#include "ort_process_context.hpp"

#include <mutex>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace corridorkey::core {

namespace {

constexpr const char* kOrtLogId = "CorridorKey";

#ifdef _WIN32
const OrtApi* load_ort_api_from_runtime_dll() {
    HMODULE runtime_module = GetModuleHandleW(L"onnxruntime.dll");
    if (runtime_module == nullptr) {
        runtime_module = LoadLibraryW(L"onnxruntime.dll");
    }
    if (runtime_module == nullptr) {
        throw std::runtime_error("ONNX Runtime DLL is not available for this ONNX execution path.");
    }

    auto* symbol = GetProcAddress(runtime_module, "OrtGetApiBase");
    if (symbol == nullptr) {
        throw std::runtime_error("ONNX Runtime DLL does not export OrtGetApiBase.");
    }

    using OrtGetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
    const auto* api_base = reinterpret_cast<OrtGetApiBaseFn>(symbol)();
    if (api_base == nullptr) {
        throw std::runtime_error("ONNX Runtime API base is null.");
    }

    const auto* api = api_base->GetApi(ORT_API_VERSION);
    if (api == nullptr) {
        throw std::runtime_error("ONNX Runtime DLL does not support the required API version.");
    }
    return api;
}
#endif

}  // namespace

void ensure_ort_api_initialized() {
    static std::once_flag init_once;
    std::call_once(init_once, [] {
#ifdef _WIN32
        Ort::InitApi(load_ort_api_from_runtime_dll());
#else
        Ort::InitApi();
#endif
    });
}

OrtProcessContext::OrtProcessContext() = default;

Ort::Env& OrtProcessContext::acquire_env(OrtLoggingLevel log_severity) {
    std::scoped_lock lock(m_mutex);
    ensure_initialized(log_severity);
    ensure_shared_cpu_allocator();
    return *m_env;
}

void OrtProcessContext::ensure_initialized(OrtLoggingLevel log_severity) {
    if (!m_initialized) {
        ensure_ort_api_initialized();
        // ORT's shared thread-pool guidance expects a process-wide env created with global
        // thread pools, leaving the pool sizes at 0 so the runtime chooses its validated
        // defaults for the host.
        m_threading_options.emplace();
        m_threading_options->SetGlobalIntraOpNumThreads(0);
        m_threading_options->SetGlobalInterOpNumThreads(0);
        m_env.emplace(*m_threading_options, log_severity, kOrtLogId);
        m_log_severity = log_severity;
        m_initialized = true;
        return;
    }

    if (log_severity != m_log_severity) {
        m_env->UpdateEnvWithCustomLogLevel(log_severity);
        m_log_severity = log_severity;
    }
}

void OrtProcessContext::ensure_shared_cpu_allocator() {
    if (m_cpu_allocator_registered) {
        return;
    }

    // Register the CPU arena allocator once at the env level so multiple sessions can reuse it
    // when they opt into `session.use_env_allocators=1`.
    m_cpu_arena_cfg.emplace(0, -1, -1, -1);
    m_cpu_memory_info.emplace(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    m_env->CreateAndRegisterAllocator(*m_cpu_memory_info, *m_cpu_arena_cfg);
    m_cpu_allocator_registered = true;
}

}  // namespace corridorkey::core
