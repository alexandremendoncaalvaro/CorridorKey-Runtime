#include "ofx_logging.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

#include "common/runtime_paths.hpp"

#if defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace corridorkey::ofx {

namespace {

std::mutex g_log_mutex;
std::FILE* g_log_file = nullptr;
bool g_log_init_attempted = false;
std::filesystem::path g_resolved_log_path;

std::filesystem::path resolve_log_path() {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_OFX_LOG");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }

#if defined(__APPLE__)
    if (auto home = common::environment_variable_copy("HOME"); home.has_value()) {
        return std::filesystem::path(*home) / "Library" / "Logs" / "CorridorKey" / "ofx.log";
    }
#elif defined(_WIN32)
    if (auto local_app_data = common::environment_variable_copy("LOCALAPPDATA");
        local_app_data.has_value()) {
        return std::filesystem::path(*local_app_data) / "CorridorKey" / "Logs" / "ofx.log";
    }
#endif

    return std::filesystem::path("corridorkey_ofx.log");
}

void ensure_parent_directory(const std::filesystem::path& path) {
    auto parent = path.parent_path();
    if (parent.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(parent, error);
}

std::string format_timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif

    char buffer[32] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_time) == 0) {
        return "0000-00-00 00:00:00";
    }
    return std::string(buffer);
}

std::uint64_t current_thread_id() {
#if defined(__APPLE__)
    std::uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentThreadId());
#else
    return 0;
#endif
}

int current_process_id() {
#if defined(_WIN32)
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

}  // namespace

void log_message(std::string_view scope, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    if (!g_log_init_attempted) {
        g_log_init_attempted = true;
        g_resolved_log_path = resolve_log_path();
        ensure_parent_directory(g_resolved_log_path);
        g_log_file = std::fopen(g_resolved_log_path.string().c_str(), "a");
    }

    if (g_log_file == nullptr) {
        return;
    }

    auto timestamp = format_timestamp();
    std::uint64_t thread_id = current_thread_id();
    int process_id = current_process_id();

    std::fprintf(g_log_file, "%s [%.*s] [pid=%d tid=%llu] %.*s\n", timestamp.c_str(),
                 static_cast<int>(scope.size()), scope.data(), process_id,
                 static_cast<unsigned long long>(thread_id), static_cast<int>(message.size()),
                 message.data());
    std::fflush(g_log_file);
}

void close_log() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file != nullptr) {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    g_log_init_attempted = false;
}

std::filesystem::path log_file_path() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_resolved_log_path.empty()) {
        g_resolved_log_path = resolve_log_path();
    }
    return g_resolved_log_path;
}

}  // namespace corridorkey::ofx
