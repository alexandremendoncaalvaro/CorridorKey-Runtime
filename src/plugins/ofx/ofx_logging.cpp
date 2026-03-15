#include "ofx_logging.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
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
    auto path = resolve_log_path();
    ensure_parent_directory(path);

    std::FILE* file = std::fopen(path.string().c_str(), "a");
    if (file == nullptr) {
        return;
    }

    auto timestamp = format_timestamp();
    std::uint64_t thread_id = current_thread_id();
    int process_id = current_process_id();

    std::fprintf(file, "%s [%.*s] [pid=%d tid=%llu] %.*s\n", timestamp.c_str(),
                 static_cast<int>(scope.size()), scope.data(), process_id,
                 static_cast<unsigned long long>(thread_id), static_cast<int>(message.size()),
                 message.data());
    std::fflush(file);
    std::fclose(file);
}

}  // namespace corridorkey::ofx
