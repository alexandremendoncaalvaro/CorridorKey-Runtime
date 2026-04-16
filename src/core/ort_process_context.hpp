#pragma once

#include <mutex>

#include "inference_session.hpp"

namespace corridorkey::core {

class OrtProcessContext {
   public:
    OrtProcessContext();

    OrtProcessContext(const OrtProcessContext&) = delete;
    OrtProcessContext& operator=(const OrtProcessContext&) = delete;
    OrtProcessContext(OrtProcessContext&&) = delete;
    OrtProcessContext& operator=(OrtProcessContext&&) = delete;

    Ort::Env& acquire_env(OrtLoggingLevel log_severity);

   private:
    void ensure_initialized(OrtLoggingLevel log_severity);
    void ensure_shared_cpu_allocator();

    std::mutex m_mutex;
    Ort::ThreadingOptions m_threading_options;
    Ort::Env m_env{nullptr};
    Ort::ArenaCfg m_cpu_arena_cfg{nullptr};
    Ort::MemoryInfo m_cpu_memory_info{nullptr};
    OrtLoggingLevel m_log_severity = ORT_LOGGING_LEVEL_ERROR;
    bool m_initialized = false;
    bool m_cpu_allocator_registered = false;
};

}  // namespace corridorkey::core
