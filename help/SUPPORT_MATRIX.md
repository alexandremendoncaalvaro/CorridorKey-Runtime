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
supported `RTX Lite` and `RTX Full` installers by default. The `DirectML`
installer is experimental and should only be published intentionally. Other
execution-provider hooks present in the core runtime, such as CUDA, WinML, and
OpenVINO, are not current product support tracks unless they are explicitly
packaged and validated.

**Windows RTX installer policy:**
- `RTX Lite` is the conservative Windows RTX installer. It packages the
  validated FP16 and INT8 ladder through `1024px`.
- `RTX Full` packages the complete FP16 ladder through `2048px`.
- `RTX Full` does not clamp a user-selected quality by VRAM policy. It attempts
  the requested packaged quality and then follows the established runtime
  failure path if that quality cannot execute.
- `RTX Lite` and `RTX Full` install to the same OFX bundle location.
  Installing one replaces the other.

The `corridorkey doctor` command reports the active backend and any fallback
conditions on your specific hardware before processing begins.

---

## OS Version Requirements

| OS | Minimum Version |
|----|----------------|
| macOS | 14.0 (Sonoma) for official support; 13.x for best-effort |
| Windows | Windows 11 for officially supported paths |
| Windows 10 | Best-effort - not systematically tested |

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
  unsupported interactive GPU requests.
