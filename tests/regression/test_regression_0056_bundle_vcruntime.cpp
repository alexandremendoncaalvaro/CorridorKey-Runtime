// Regression test for Issue #56:
// "Failed to prepare runtime session for ... OFX runtime server process exited
//  during startup" reported on Foundry Nuke 17.0v2 + Windows + RTX 3090 Ti
//  against v0.8.2-win.2.
//
// The OFX runtime sidecar imported MSVCP140.dll from Foundry Nuke's install
// directory instead of from %WINDIR%\System32 because Nuke calls
// SetDllDirectory on its own process — Microsoft documents at
// https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order
// that this alters the DLL search order INHERITED BY CHILD PROCESSES, placing
// the host install dir ahead of System32. Nuke's older MSVCP140 v14.36 was
// ABI-incompatible with the toolchain TensorRT-RTX / ORT were built against
// (v14.50), and the spawned sidecar crashed at MSVCP140!0x12f58 with
// EXCEPTION_ACCESS_VIOLATION before reaching wWinMain (verified against the
// Windows Application event log).
//
// The fix is the deployment Microsoft documents at
// https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
// under "Install individual redistributable files": ship the Visual C++
// Redistributable DLLs app-local in the bundle. CMake's
// InstallRequiredSystemLibraries module discovers the active toolchain's
// redist set and src/plugins/ofx/CMakeLists.txt copies it into the bundle's
// Contents/Win64/. Win32 search-order step #1 ("the folder from which the
// application loaded") is evaluated BEFORE any SetDllDirectory-altered step,
// so the bundled copy wins regardless of host process behavior.
//
// This regression guard asserts the bundle still ships the redist set on
// every Windows build. If a future refactor strips the POST_BUILD copy from
// CMake or drops the InstallRequiredSystemLibraries include from the root
// configuration, this test fails the build before anyone packages a release
// that reproduces #56.
//
// The CMake build wires CORRIDORKEY_OFX_BUNDLE_WIN64_DIR to the staged
// bundle's Contents/Win64 path. The test runs cross-platform (no GPU, no
// host-specific behavior) and is a no-op outside Windows because the
// VC++ Redist concept is Windows-only.

#include <array>
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <string>

#if defined(_WIN32)

namespace {

// Names that must be staged app-local in the OFX bundle's Contents/Win64
// directory. Mirrors CorridorKeyExpectedBundledRuntimeList in
// scripts/validate_ofx_win.ps1; they are the names CMake's
// InstallRequiredSystemLibraries discovers from the active MSVC toolchain's
// VS 2022 v143 C++ redistributable component.
//
// MSVCP140_atomic_wait.dll and MSVCP140_codecvt_ids.dll are absent from older
// toolchains (VS 2022 < 17.5). Treat them as informational rather than
// blocking; the core MSVCP140 + VCRUNTIME140 entries are what protect against
// the Issue #56 crash.
constexpr std::array<const char*, 5> kRequiredRedistNames = {
    "MSVCP140.dll",
    "MSVCP140_1.dll",
    "MSVCP140_2.dll",
    "VCRUNTIME140.dll",
    "VCRUNTIME140_1.dll",
};

}  // namespace

TEST_CASE("Regression #56: OFX bundle ships Visual C++ Redistributable app-local",
          "[regression][packaging][windows]") {
#if !defined(CORRIDORKEY_OFX_BUNDLE_WIN64_DIR)
    SKIP("CORRIDORKEY_OFX_BUNDLE_WIN64_DIR not defined; bundle staging path "
         "not propagated by CMake (e.g. when the test target was built without "
         "corridorkey_ofx as a dependency).");
#else
    const std::filesystem::path bundle_win64_dir = CORRIDORKEY_OFX_BUNDLE_WIN64_DIR;

    INFO("Bundle Contents/Win64 directory: " << bundle_win64_dir.string());
    REQUIRE(std::filesystem::exists(bundle_win64_dir));
    REQUIRE(std::filesystem::is_directory(bundle_win64_dir));

    for (const char* name : kRequiredRedistNames) {
        const auto candidate = bundle_win64_dir / name;
        INFO("Expecting app-local " << name << " at " << candidate.string()
             << " (Issue #56: required to win Win32 DLL search step #1 over "
             << "Foundry Nuke's app-local MSVCP140 inherited via SetDllDirectory)");
        REQUIRE(std::filesystem::exists(candidate));

        // Sanity: the file must be a non-trivial PE image, not a zero-byte
        // placeholder. Sizes observed on a VS 2022 v143 toolchain
        // (cl.exe v14.44.35211) for reference:
        //   MSVCP140.dll              ~ 558 KB
        //   MSVCP140_1.dll            ~  36 KB  (atomic types satellite)
        //   MSVCP140_2.dll            ~ 280 KB
        //   VCRUNTIME140.dll          ~ 124 KB
        //   VCRUNTIME140_1.dll        ~  50 KB
        // A 4 KB lower bound rules out empty/stub placeholders without
        // tripping on the smaller satellites; the 5 MB upper bound flags
        // a future toolchain that would ship something dramatically
        // different from anything we have seen.
        const auto size = std::filesystem::file_size(candidate);
        CHECK(size > 4 * 1024);
        CHECK(size < 5 * 1024 * 1024);
    }
#endif
}

#else  // !_WIN32

TEST_CASE("Regression #56: OFX bundle ships Visual C++ Redistributable app-local",
          "[regression][packaging][windows]") {
    SUCCEED("Visual C++ Redistributable bundling only applies to Windows; "
            "non-Windows builds are not affected by Issue #56.");
}

#endif  // _WIN32
