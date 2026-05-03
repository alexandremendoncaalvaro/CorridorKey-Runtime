// torch-free TU. Compiled into corridorkey_core so the base runtime can
// arm the TorchTRT runtime BEFORE it invokes any symbol from the delay-
// loaded corridorkey_torchtrt.dll. See torch_trt_loader.hpp for the
// rationale (Strategy C, Sprint 1 PR 1 follow-up).

#include "torch_trt_loader.hpp"

#include <atomic>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
// clang-format off
// windows.h must precede LoadLibraryEx & friends; provides
// AddDllDirectory, SetDefaultDllDirectories, GetLastError, HMODULE.
#include <windows.h>
// clang-format on
#endif

namespace corridorkey::core {

namespace {

// Maximum number of ancestor directories we walk when looking for a
// vendor/torchtrt-windows/bin layout. 8 covers the deepest known dev
// tree layout (build/<preset>/tests/integration/<bin>/foo.ts) plus a
// margin.
constexpr int kMaxAncestorWalkDepth = 8;

#ifdef _WIN32
// One-shot arming flag. Wrapped in a function-local atomic accessor so
// the cppcoreguidelines-avoid-non-const-global-variables check stays
// happy: the atomic itself is const-init on first call, and the arming
// is idempotent (the OS loader keeps the DLL once loaded).
std::atomic_bool& torchtrt_runtime_armed_flag() {
    static std::atomic_bool flag{false};
    return flag;
}
#endif

}  // namespace

std::filesystem::path resolve_torchtrt_runtime_bin(const std::filesystem::path& ts_path) {
#ifdef _WIN32
    if (const char* env = std::getenv("CORRIDORKEY_TORCHTRT_RUNTIME_DIR")) {
        std::filesystem::path candidate{env};
        if (std::filesystem::exists(candidate / "torchtrt.dll")) {
            return candidate;
        }
    }
    // Absolutise first: relative paths (e.g. "models/foo.ts") yield an
    // empty parent_path() chain after one or two steps, breaking the
    // walk loop before it reaches the repo root or any blue-pack-shaped
    // layout.
    std::error_code error;
    auto absolute_ts = std::filesystem::absolute(ts_path, error);
    if (error) {
        absolute_ts = ts_path;
    }

    auto pack_relative = absolute_ts.parent_path().parent_path() / "torchtrt-runtime" / "bin";
    if (std::filesystem::exists(pack_relative / "torchtrt.dll")) {
        return pack_relative;
    }
    auto walk = absolute_ts.parent_path();
    for (int depth = 0; depth < kMaxAncestorWalkDepth && !walk.empty() && walk != walk.root_path();
         ++depth) {
        auto candidate = walk / "vendor" / "torchtrt-windows" / "bin";
        if (std::filesystem::exists(candidate / "torchtrt.dll")) {
            return candidate;
        }
        walk = walk.parent_path();
    }
    auto root_candidate = walk / "vendor" / "torchtrt-windows" / "bin";
    if (std::filesystem::exists(root_candidate / "torchtrt.dll")) {
        return root_candidate;
    }
    return {};
#else
    (void)ts_path;
    return {};
#endif
}

bool arm_torchtrt_runtime(const std::filesystem::path& bin_dir, std::string& out_error) {
#ifdef _WIN32
    if (torchtrt_runtime_armed_flag().load(std::memory_order_acquire)) {
        return true;
    }
    const auto absolute = std::filesystem::absolute(bin_dir);
    auto* cookie = AddDllDirectory(absolute.wstring().c_str());
    if (cookie == nullptr) {
        out_error = "AddDllDirectory failed for " + absolute.string() +
                    " (GetLastError=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

    // Pre-load torchtrt.dll so the delay-load resolver inside
    // corridorkey_torchtrt.dll finds a cached HMODULE on its first
    // touch (and so the TensorRT plugin static initialisers register
    // their custom ops).
    const auto torchtrt_path = (absolute / L"torchtrt.dll").wstring();
    HMODULE torchtrt_handle =
        LoadLibraryExW(torchtrt_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (torchtrt_handle == nullptr) {
        out_error = "LoadLibrary torchtrt.dll failed (GetLastError=" +
                    std::to_string(GetLastError()) + ")";
        return false;
    }

    // Pre-load corridorkey_torchtrt.dll (the SHARED wrapper that owns
    // the C++ TorchTrtSession class). The base exe / OFX bundle links
    // its IMPORT lib with /DELAYLOAD; without this LoadLibraryExW the
    // delay-load helper would fall back to the OS default search,
    // which does not include the blue pack dir we just added via
    // AddDllDirectory. The cached HMODULE makes the next implicit
    // resolve a hit.
    //
    // The DLL sits next to torchtrt.dll in both the dev build (POST_
    // BUILD copy from src/core into vendor/torchtrt-windows/bin) and
    // the blue pack (`<pack>/torchtrt-runtime/bin/`).
    const auto wrapper_path = (absolute / L"corridorkey_torchtrt.dll").wstring();
    if (std::filesystem::exists(wrapper_path)) {
        HMODULE wrapper_handle =
            LoadLibraryExW(wrapper_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (wrapper_handle == nullptr) {
            out_error = "LoadLibrary corridorkey_torchtrt.dll failed (GetLastError=" +
                        std::to_string(GetLastError()) + ")";
            return false;
        }
    }
    // If the wrapper DLL is not next to torchtrt.dll, leave resolution
    // to the loader's default rules; the arm still succeeded for the
    // torch / TensorRT side, and downstream consumers may be linking
    // the wrapper from a different staging dir (e.g. dev test runs).

    torchtrt_runtime_armed_flag().store(true, std::memory_order_release);
    return true;
#else
    (void)bin_dir;
    (void)out_error;
    return true;
#endif
}

}  // namespace corridorkey::core
