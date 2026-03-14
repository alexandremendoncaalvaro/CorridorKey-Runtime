#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <delayimp.h>
#include <string>

static FARPROC WINAPI corridorkey_delay_load_hook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        if (_stricmp(pdli->szDll, "onnxruntime.dll") == 0) {
            HMODULE hPlugin = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&corridorkey_delay_load_hook), &hPlugin);

            if (hPlugin) {
                wchar_t path[MAX_PATH];
                GetModuleFileNameW(hPlugin, path, MAX_PATH);
                std::wstring plugin_path(path);
                auto last_slash = plugin_path.find_last_of(L"\\/");
                if (last_slash != std::wstring::npos) {
                    std::wstring dir = plugin_path.substr(0, last_slash + 1);
                    std::wstring custom_dll = dir + L"corridorkey_onnxruntime.dll";
                    HMODULE hMod = LoadLibraryW(custom_dll.c_str());
                    if (hMod) {
                        return reinterpret_cast<FARPROC>(hMod);
                    }
                }
            }
        }
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = corridorkey_delay_load_hook;

#endif
