#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
// delayimp.h depends on Windows types/macros from windows.h.
#include <windows.h>
#include <delayimp.h>
// clang-format on

#include <chrono>
#include <ctime>
#include <fstream>
#include <string>

namespace {

void debug_log(const std::wstring& message) {
    WCHAR temp_path[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp_path) == 0) {
        return;
    }

    std::wstring log_path = std::wstring(temp_path) + L"corridorkey_ofx_delayload.log";

    std::wofstream log_file(log_path, std::ios::app);
    if (!log_file.is_open()) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
    localtime_s(&local_time, &time_t_now);

    wchar_t timestamp[64];
    if (std::wcsftime(timestamp, 64, L"%Y-%m-%d %H:%M:%S", &local_time) > 0) {
        log_file << timestamp << L" ";
    }

    log_file << message << std::endl;
}

}  // namespace

static FARPROC WINAPI corridorkey_delay_load_hook(unsigned dliNotify, PDelayLoadInfo pdli) {
    debug_log(L"[HOOK] Delay-load hook called");

    if (dliNotify == dliNotePreLoadLibrary) {
        // Convert DLL name from char* to wstring for logging
        size_t dll_name_len = strlen(pdli->szDll);
        std::wstring dll_name_wide(dll_name_len, L'\0');
        mbstowcs(&dll_name_wide[0], pdli->szDll, dll_name_len);
        debug_log(L"[HOOK] DLL: " + dll_name_wide);

        if (_stricmp(pdli->szDll, "onnxruntime.dll") == 0) {
            debug_log(L"[HOOK] Intercepted onnxruntime.dll load request");

            HMODULE hPlugin = nullptr;
            BOOL result = GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&corridorkey_delay_load_hook), &hPlugin);

            if (!result) {
                DWORD error = GetLastError();
                debug_log(L"[ERROR] GetModuleHandleExW failed with error code: " +
                          std::to_wstring(error));
                return nullptr;
            }

            debug_log(L"[OK] Got plugin module handle");

            wchar_t path[MAX_PATH];
            DWORD length = GetModuleFileNameW(hPlugin, path, MAX_PATH);
            if (length == 0) {
                DWORD error = GetLastError();
                debug_log(L"[ERROR] GetModuleFileNameW failed with error code: " +
                          std::to_wstring(error));
                return nullptr;
            }

            std::wstring plugin_path(path);
            debug_log(L"[OK] Plugin path: " + plugin_path);

            auto last_slash = plugin_path.find_last_of(L"\\/");
            if (last_slash == std::wstring::npos) {
                debug_log(L"[ERROR] No path separator found in plugin path");
                return nullptr;
            }

            std::wstring dir = plugin_path.substr(0, last_slash + 1);
            std::wstring local_dll = dir + L"onnxruntime.dll";

            debug_log(L"[ATTEMPT] Loading local DLL: " + local_dll);

            HMODULE hMod = LoadLibraryW(local_dll.c_str());
            if (hMod) {
                debug_log(L"[SUCCESS] Loaded local onnxruntime.dll");
                return reinterpret_cast<FARPROC>(hMod);
            }

            DWORD error = GetLastError();
            debug_log(L"[ERROR] LoadLibraryW failed with error code: " + std::to_wstring(error));
            debug_log(L"[ERROR] Expected path: " + local_dll);
            return nullptr;
        }
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = corridorkey_delay_load_hook;

// DllMain for early initialization logging
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        debug_log(L"[DllMain] CorridorKey OFX plugin DLL loaded (DLL_PROCESS_ATTACH)");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        debug_log(L"[DllMain] CorridorKey OFX plugin DLL unloading (DLL_PROCESS_DETACH)");
    }

    return TRUE;
}

#endif
