#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <memory>

// Cross-DLL visibility for the TorchTrtSession class.
//
// Strategy C, Sprint 1 PR 1 follow-up: torch_trt_session.cpp lives
// inside the SHARED corridorkey_torchtrt.dll so the base runtime stays
// torch-free and only pays the libtorch DLL load when a Backend::
// TorchTRT session is actually created (delay-loaded, see
// src/core/CMakeLists.txt and src/core/torch_trt_loader.cpp).
//
// The macro flips between dllexport (when building corridorkey_torchtrt)
// and dllimport (when consumed by corridorkey_core / cli / ofx /
// tests). Without the explicit attribute MSVC skips the import-table
// entries and /DELAYLOAD becomes a no-op (LNK4199).
#if defined(_WIN32)
#  if defined(CORRIDORKEY_TORCHTRT_BUILDING)
#    define CORRIDORKEY_TORCHTRT_API __declspec(dllexport)
#  else
#    define CORRIDORKEY_TORCHTRT_API __declspec(dllimport)
#  endif
#else
#  define CORRIDORKEY_TORCHTRT_API
#endif

namespace corridorkey::core {

// Wraps a Torch-TensorRT compiled .ts engine for in-process forward.
// Sister to MlxSession - same wrapping pattern used by InferenceSession,
// gated on Backend::TorchTRT. Built only when CORRIDORKEY_HAS_TORCHTRT is
// defined (vendor/torchtrt-windows/ staged via prepare-torchtrt).
//
// Strategy C, Sprint 1: this session is the runtime entrypoint for the
// blue model pack on Windows RTX. Engines are compiled with
// hardware_compatible=True so the same .ts runs on RTX 30/40/50.
//
// MSVC C4251 suppression: the std::unique_ptr<Impl> private member
// triggers C4251 ("needs dll-interface to be used by clients") when
// the class is __declspec(dllexport)/dllimport'd. The warning is
// harmless here because Impl is forward-declared and never accessed
// across the DLL boundary -- consumers only call the public member
// functions, all of which are compiled inside corridorkey_torchtrt.dll
// and thunk through to Impl internally. Same suppression pattern Apple
// and PyTorch ship in their own libtorch headers.
#if defined(_WIN32) && defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4251)
#endif
class CORRIDORKEY_TORCHTRT_API TorchTrtSession {
   public:
    static Result<std::unique_ptr<TorchTrtSession>> create(const std::filesystem::path& ts_path,
                                                           const DeviceInfo& device,
                                                           StageTimingCallback on_stage = nullptr);

    ~TorchTrtSession();

    TorchTrtSession(const TorchTrtSession&) = delete;
    TorchTrtSession& operator=(const TorchTrtSession&) = delete;
    TorchTrtSession(TorchTrtSession&&) noexcept;
    TorchTrtSession& operator=(TorchTrtSession&&) noexcept;

    [[nodiscard]] Result<FrameResult> infer(const Image& rgb, const Image& alpha_hint,
                                            bool output_alpha_only = false,
                                            StageTimingCallback on_stage = nullptr);

    [[nodiscard]] int model_resolution() const;

   private:
    TorchTrtSession();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
#if defined(_WIN32) && defined(_MSC_VER)
#  pragma warning(pop)
#endif

}  // namespace corridorkey::core
