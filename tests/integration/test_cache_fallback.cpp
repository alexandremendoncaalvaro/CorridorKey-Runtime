#include <catch2/catch_all.hpp>
#include <corridorkey/types.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Aclapi.h>
#include <Windows.h>
#endif

#include "app/job_orchestrator.hpp"
#include "core/inference_session.hpp"

using namespace corridorkey;
using namespace corridorkey::app;

namespace {

std::optional<std::string> environment_variable_copy(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return std::nullopt;
    }

    std::string copy(value, length > 0 ? length - 1 : 0);
    std::free(value);
    return copy;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }

    return std::string(value);
#endif
}

class ScopedEnvVar {
   public:
    ScopedEnvVar(const char* name, std::string value) : m_name(name) {
        auto current = environment_variable_copy(name);
        if (current.has_value()) {
            m_previous = *current;
        }

#ifdef _WIN32
        _putenv_s(m_name.c_str(), value.c_str());
#else
        setenv(m_name.c_str(), value.c_str(), 1);
#endif
    }

    ~ScopedEnvVar() {
#ifdef _WIN32
        _putenv_s(m_name.c_str(), m_previous.c_str());
#else
        if (m_previous.empty()) {
            unsetenv(m_name.c_str());
        } else {
            setenv(m_name.c_str(), m_previous.c_str(), 1);
        }
#endif
    }

   private:
    std::string m_name;
    std::string m_previous = {};
};

class ScopedPermissions {
   public:
    ScopedPermissions(std::filesystem::path path, std::filesystem::perms restore_perms)
        : m_path(std::move(path)), m_restore_perms(restore_perms) {}

    ~ScopedPermissions() {
        std::error_code error;
        std::filesystem::permissions(m_path, m_restore_perms,
                                     std::filesystem::perm_options::replace, error);
    }

   private:
    std::filesystem::path m_path;
    std::filesystem::perms m_restore_perms;
};

#ifdef _WIN32
class ScopedHandle {
   public:
    explicit ScopedHandle(HANDLE handle) : m_handle(handle) {}
    ~ScopedHandle() {
        if (m_handle) {
            CloseHandle(m_handle);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

   private:
    HANDLE m_handle = nullptr;
};

class ScopedWindowsDenyWrite {
   public:
    explicit ScopedWindowsDenyWrite(const std::filesystem::path& path)
        : m_path(path), m_path_wide(path.wstring()) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return;
        }
        ScopedHandle token_guard(token);

        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
            return;
        }

        std::vector<unsigned char> buffer(size);
        if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
            return;
        }

        auto* token_user = reinterpret_cast<TOKEN_USER*>(buffer.data());
        if (token_user == nullptr || token_user->User.Sid == nullptr) {
            return;
        }

        DWORD result = GetNamedSecurityInfoW(
            m_path_wide.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
            &m_original_dacl, nullptr, &m_original_sd);
        if (result != ERROR_SUCCESS) {
            return;
        }

        EXPLICIT_ACCESSW deny = {};
        deny.grfAccessPermissions = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_WRITE_DATA |
                                    FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                    DELETE;
        deny.grfAccessMode = DENY_ACCESS;
        deny.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        deny.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        deny.Trustee.TrusteeType = TRUSTEE_IS_USER;
        deny.Trustee.ptstrName = reinterpret_cast<LPWSTR>(token_user->User.Sid);

        PACL new_dacl = nullptr;
        result = SetEntriesInAclW(1, &deny, m_original_dacl, &new_dacl);
        if (result != ERROR_SUCCESS || new_dacl == nullptr) {
            return;
        }

        result = SetNamedSecurityInfoW(m_path_wide.data(), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl,
                                       nullptr);
        LocalFree(new_dacl);
        if (result != ERROR_SUCCESS) {
            return;
        }

        m_active = true;
    }

    ~ScopedWindowsDenyWrite() {
        if (!m_original_sd) {
            return;
        }

        if (m_active) {
            SetNamedSecurityInfoW(m_path_wide.data(), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION, nullptr, nullptr, m_original_dacl,
                                  nullptr);
        }

        LocalFree(m_original_sd);
    }

    [[nodiscard]] bool is_active() const {
        return m_active;
    }

    ScopedWindowsDenyWrite(const ScopedWindowsDenyWrite&) = delete;
    ScopedWindowsDenyWrite& operator=(const ScopedWindowsDenyWrite&) = delete;

   private:
    std::filesystem::path m_path;
    std::wstring m_path_wide;
    bool m_active = false;
    PACL m_original_dacl = nullptr;
    PSECURITY_DESCRIPTOR m_original_sd = nullptr;
};
#endif
}  // namespace

TEST_CASE("session creation falls back to writable cache root when configured cache is locked",
          "[integration][cache]") {
    const std::filesystem::path model_path = "models/corridorkey_int8_512.onnx";
    if (!std::filesystem::exists(model_path)) {
        SKIP("Model not available");
    }

    const std::filesystem::path models_dir = std::filesystem::path(PROJECT_ROOT) / "models";
    const std::filesystem::path locked_cache_dir =
        std::filesystem::path(PROJECT_ROOT) / "build" / "test_cache_locked";

    std::error_code error;
    std::filesystem::remove_all(locked_cache_dir, error);
    std::filesystem::create_directories(locked_cache_dir, error);
    REQUIRE_FALSE(error);

#ifdef _WIN32
    ScopedWindowsDenyWrite deny_write(locked_cache_dir);
    REQUIRE(deny_write.is_active());
#else
    ScopedPermissions restore_permissions(locked_cache_dir, std::filesystem::perms::owner_all);
    std::filesystem::permissions(
        locked_cache_dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, error);
    REQUIRE_FALSE(error);
#endif

    ScopedEnvVar cache_override("CORRIDORKEY_CACHE_DIR", locked_cache_dir.string());

    auto report = JobOrchestrator::run_doctor(models_dir);
    REQUIRE(report["cache"]["configured_path"] == locked_cache_dir.string());
    REQUIRE(report["cache"]["writable"].get<bool>());
    REQUIRE(report["cache"]["fallback_in_use"].get<bool>());
    REQUIRE_FALSE(report["cache"]["selected_path"].get<std::string>().empty());
    REQUIRE(report["cache"]["selected_path"] != locked_cache_dir.string());
    REQUIRE(report["cache"]["coreml_ep_cache_dir"].get<std::string>().rfind(
                report["cache"]["selected_path"].get<std::string>(), 0) == 0);

    auto session_res =
        InferenceSession::create(model_path, DeviceInfo{"Generic CPU", 0, Backend::CPU});
    REQUIRE(session_res.has_value());

#ifndef _WIN32
    std::filesystem::permissions(locked_cache_dir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
#endif
    std::filesystem::remove_all(locked_cache_dir, error);
}
