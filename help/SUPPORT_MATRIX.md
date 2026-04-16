# Support Matrix

This document is the single source of truth for hardware, platform, and host
application support policy. Every hardware path and host version has an
explicit designation. No path is described without one.

**See also:**
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) - practical guide for diagnosing
issues on any supported or partially supported path

Support status is defined by packaged and validated product tracks. A backend
enum, probe, or provider hook in the core runtime does not become a support
claim by itself.

---

## Support Designations

| Designation | Meaning |
|-------------|---------|
| **Officially supported** | Validated on this configuration. Releases are tested against it. Bug reports are accepted and prioritized. |
| **Best-effort** | Known to work in many cases, but not systematically validated. Known limitations exist. Bug reports are accepted but not guaranteed to be resolved. |
| **Experimental** | Partially integrated. Known errors exist in practice. Not recommended for production use. Bug reports are accepted for tracking purposes only. |
| **Unsupported** | Not integrated as a product track or known to be broken. No bug reports accepted. |

---

## DaVinci Resolve - Host Version Support

| Resolve Version | OFX Plugin Support |
|-----------------|-------------------|
| DaVinci Resolve 20 | Officially supported |
| DaVinci Resolve 20 on Linux (Studio only) | Experimental |
| DaVinci Resolve 19 | Best-effort |
| DaVinci Resolve 18 | Best-effort - known plugin discovery and loading issues exist; behavior is not equivalent to Resolve 20 |
| DaVinci Resolve 17 and earlier | Unsupported |

Resolve 18 has real plugin discovery and loading issues that have not been
resolved. It must not be treated as equivalent to Resolve 20. If the plugin
does not appear in Resolve 18, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## macOS - Platform and Hardware Support

| Configuration | Support |
|---------------|---------|
| Apple Silicon (M1, M2, M3, M4, M5) - macOS 14+ | Officially supported |
| Apple Silicon - macOS 13 | Best-effort |
| Intel Mac | Unsupported |

Apple Silicon is the primary and official macOS path. The MLX backend is used
for inference on all M-series chips. A CoreML path exists in the core runtime,
but it is not the primary product track.

---

## Windows - Platform and Hardware Support

Windows support is defined per backend and hardware family. The platform as a
whole does not have a single designation.

| Hardware | Backend | Support |
|----------|---------|---------|
| NVIDIA Ampere (RTX 30 series) | TensorRT RTX EP | Officially supported |
| NVIDIA Ada Lovelace (RTX 40 series) | TensorRT RTX EP | Officially supported |
| NVIDIA Blackwell (RTX 50 series) | TensorRT RTX EP | Officially supported |
| NVIDIA RTX 20 series (Turing) | TensorRT RTX EP | Experimental - implemented but not validated as an official product path |
| NVIDIA GTX 10xx / 16xx | CUDA via ONNX Runtime | Unsupported - a core CUDA path exists, but no packaged Windows CUDA product track is distributed |
| Intel integrated GPU (DirectX 12) | DirectML | Experimental - implemented and distributed, but not validated as an official product path |
| Intel Arc discrete GPU | DirectML | Experimental - implemented and distributed, but not validated as an official product path |
| AMD GPU | DirectML | Experimental - known errors in practice |
| CPU (AVX2+) | ONNX CPU | Best-effort fallback path for CLI and tolerant workflows |

**RTX 20 series (Turing):** A TensorRT RTX EP path exists in code and may be
usable on some systems, but it is not a validated official Windows support
track. Do not rely on it for production use.

**AMD:** DirectML integration exists but known errors occur in practice.
AMD GPUs are not officially supported. Do not rely on them for production use.

**Windows product tracks:** The canonical public Windows release emits the
official `Windows RTX` installer by default. The `DirectML` installer is
experimental and should only be published intentionally. Other
execution-provider hooks present in the core runtime, such as CUDA, WinML, and
OpenVINO, are not current product support tracks unless they are explicitly
packaged and validated.

**Windows RTX installer policy:**
- `Windows RTX` is the official Windows installer for NVIDIA RTX 30 series and
  newer. It packages the complete FP16 ladder through `2048px` and includes
  the portable INT8 CPU artifacts.
- In `Auto`, `Windows RTX` respects the current safe quality ceiling for the
  detected VRAM tier.
- In fixed modes, `Windows RTX` can attempt a packaged quality above the safe
  ceiling and then follows the established runtime failure or fallback path if
  that quality cannot execute.
- `Windows RTX` installs to a single OFX bundle location. Reinstalling the
  same track replaces the previous Windows RTX package in place.
- The current public Windows RTX quality ladder in the OFX plugin is
  `Draft (512)`, `High (1024)`, `Ultra (1536)`, and `Maximum (2048)`.
  The historical `768px` rung remains reference-only and is not part of the
  public OFX quality UI.

The `corridorkey doctor` command reports the active backend and any fallback
conditions on your specific hardware before processing begins.

---

## Linux - Platform and Hardware Support

Linux support is an experimental product track. It packages the same FP16
model ladder shipped by the Windows RTX track, but switches the inference
backend to the ONNX Runtime CUDA Execution Provider via a curated Microsoft
prebuilt (`onnxruntime-linux-x64-gpu`). The TensorRT RTX Execution Provider is
not yet built for Linux and is tracked for a future release.

| Hardware | Backend | Support |
|----------|---------|---------|
| NVIDIA Ampere (RTX 30 series) | CUDA EP via ONNX Runtime | Experimental |
| NVIDIA Ada Lovelace (RTX 40 series) | CUDA EP via ONNX Runtime | Experimental |
| NVIDIA Blackwell (RTX 50 series) | CUDA EP via ONNX Runtime | Experimental |
| NVIDIA RTX 20 series (Turing) | CUDA EP via ONNX Runtime | Experimental |
| AMD GPU | none | Unsupported - no ROCm product track is packaged |
| Intel GPU | none | Unsupported - no OneAPI product track is packaged |
| CPU (AVX2+) | ONNX CPU | Best-effort fallback path for CLI and tolerant workflows |

Linux packaging emits three artifacts from the same validated bundle:

- `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.tar.gz` - universal portable archive
  with `install.sh` and `uninstall.sh` helpers.
- `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.deb` - Debian package for Ubuntu
  22.04 LTS and Ubuntu 24.04 LTS.
- `CorridorKey_Resolve_vX.Y.Z_Linux_RTX.rpm` - RPM package for Rocky Linux 9
  and RHEL 9.

All three wrappers install the same bundle at
`/usr/OFX/Plugins/CorridorKey.ofx.bundle/` and register `corridorkey` on the
system `PATH` through `/usr/local/bin/corridorkey`.

**NVIDIA driver requirement:** Linux installs require a proprietary NVIDIA
driver of version 555 or newer. The CUDA Toolkit itself does not need to be
installed on the host; the packaged runtime ships the CUDA user-mode runtime
libraries next to the plugin binaries.

**CI coverage gap:** Linux builds run on hosted Ubuntu runners without GPU
access. GPU-side validation is not currently covered by automated CI. This is
the primary reason the Linux track is designated Experimental.

---

## OS Version Requirements

| OS | Minimum Version |
|----|----------------|
| macOS | 14.0 (Sonoma) for official support; 13.x for best-effort |
| Windows | Windows 11 for officially supported paths |
| Windows 10 | Best-effort - not systematically tested |
| Rocky Linux / RHEL | 9.x - Experimental (matches Blackmagic Design's own Resolve Studio 20 host target) |
| Ubuntu LTS | 22.04 and 24.04 - Experimental |
| Other Linux distributions | Unsupported - builds may work via the `.tar.gz` archive but are not validated |

---

## CLI vs OFX Plugin Support Scope

The CLI and OFX plugin share the same execution backends and hardware support
designations above. The OFX plugin additionally depends on the host
application support defined in the host version table above.

The CLI does not have a host application dependency and can be used
independently of any NLE.

CPU fallback is also surface-dependent:

- CLI and tolerant automation workflows may fall back to the ONNX CPU path.
- The OFX plugin prefers explicit failure over silent CPU fallback on
  unsupported interactive GPU requests. CPU fallback only happens there when
  the advanced `Allow CPU Fallback` option is enabled.
