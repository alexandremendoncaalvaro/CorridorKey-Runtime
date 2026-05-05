#pragma once

#include <filesystem>
#include <string>

namespace corridorkey::core {

// Resolves the directory holding the TorchTRT runtime DLLs (torchtrt.dll
// and the libtorch / TensorRT / CUDA deps it transitively pulls in) for a
// given .ts engine path. Returns an empty path if no candidate dir is
// found.
//
// Lookup order (Strategy C, Sprint 1):
//   1. CORRIDORKEY_TORCHTRT_RUNTIME_DIR env override (dev / CI).
//   2. <ts file dir>/../torchtrt-runtime/bin (blue pack on-disk layout).
//   3. <repo>/vendor/torchtrt-windows/bin (dev fallback when running
//      from the build tree before a pack is assembled).
[[nodiscard]] std::filesystem::path resolve_torchtrt_runtime_bin(
    const std::filesystem::path& ts_path);

// Arms the TorchTRT runtime so subsequent `torch::jit::load` /
// torchtrt-emitted ops can find their DLLs:
//
//   - AddDllDirectory(<bin_dir>) so the OS loader honors blue-pack
//     paths for any later LoadLibrary.
//   - LoadLibraryEx("torchtrt.dll", LOAD_WITH_ALTERED_SEARCH_PATH) to
//     pre-populate the loader cache so the delay-loaded
//     corridorkey_torchtrt.dll can resolve its torch / torchtrt /
//     tensorrt deps.
//   - LoadLibraryEx("corridorkey_torchtrt.dll") so the consumer's
//     /DELAYLOAD stub finds a cached HMODULE on its first call.
//
// Returns true on success; on failure, ``out_error`` contains a
// human-readable diagnostic and the function may safely be called
// again. Idempotent: a second arm with the same dir is a no-op.
//
// On non-Windows builds this is a no-op that returns true. The function
// is exposed in the torch-free TU `torch_trt_loader.cpp` so the base
// runtime can call it before invoking any symbol from the delay-loaded
// `corridorkey_torchtrt.dll`. Putting it inside `torch_trt_session.cpp`
// (Sprint 1 PR 3 layout) made the arming itself a delay-load trigger,
// defeating the AddDllDirectory hop.
[[nodiscard]] bool arm_torchtrt_runtime(const std::filesystem::path& bin_dir,
                                        std::string& out_error);

}  // namespace corridorkey::core
