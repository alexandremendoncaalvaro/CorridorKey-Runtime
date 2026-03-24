# Support Matrix

This document is the single source of truth for hardware, platform, and host
application support policy. Every hardware path and host version has an
explicit designation. No path is described without one.

**See also:**
[TROUBLESHOOTING.md](TROUBLESHOOTING.md) — practical guide for diagnosing
issues on any supported or partially supported path

---

## Support Designations

| Designation | Meaning |
|-------------|---------|
| **Officially supported** | Validated on this configuration. Releases are tested against it. Bug reports are accepted and prioritized. |
| **Best-effort** | Known to work in many cases, but not systematically validated. Known limitations exist. Bug reports are accepted but not guaranteed to be resolved. |
| **Experimental** | Partially integrated. Known errors exist in practice. Not recommended for production use. Bug reports are accepted for tracking purposes only. |
| **Unsupported** | Not integrated or known to be broken. No bug reports accepted. |

---

## DaVinci Resolve — Host Version Support

| Resolve Version | OFX Plugin Support |
|-----------------|-------------------|
| DaVinci Resolve 20 | Officially supported |
| DaVinci Resolve 19 | Best-effort |
| DaVinci Resolve 18 | Best-effort — known plugin discovery and loading issues exist; behavior is not equivalent to Resolve 20 |
| DaVinci Resolve 17 and earlier | Unsupported |

Resolve 18 has real plugin discovery and loading issues that have not been
resolved. It must not be treated as equivalent to Resolve 20. If the plugin
does not appear in Resolve 18, see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## macOS — Platform and Hardware Support

| Configuration | Support |
|---------------|---------|
| Apple Silicon (M1, M2, M3, M4) — macOS 14+ | Officially supported |
| Apple Silicon — macOS 13 | Best-effort |
| Intel Mac | Unsupported |

Apple Silicon is the primary and official macOS path. The MLX backend is used
for inference on all M-series chips.

---

## Windows — Platform and Hardware Support

Windows support is defined per backend and hardware family. The platform as a
whole does not have a single designation.

| Hardware | Backend | Support |
|----------|---------|---------|
| NVIDIA Ampere (RTX 30 series) | TensorRT | Officially supported |
| NVIDIA Ada Lovelace (RTX 40 series) | TensorRT | Officially supported |
| NVIDIA Blackwell (RTX 50 series) | TensorRT | Best-effort |
| NVIDIA RTX 20 series (Turing) | TensorRT | Experimental — known errors in practice |
| NVIDIA GTX 10xx / 16xx | CUDA via ONNX Runtime | Experimental |
| Intel integrated GPU (DirectX 12) | DirectML | Best-effort |
| Intel Arc discrete GPU | DirectML | Best-effort |
| AMD GPU | DirectML | Experimental — known errors in practice |
| CPU (AVX2+) | ONNX CPU | Best-effort (fallback path) |

**RTX 20 series (Turing):** TensorRT integration exists but known errors occur
in practice on this hardware generation. RTX 20 series is not officially
supported. Do not rely on it for production use.

**AMD:** DirectML integration exists but known errors occur in practice.
AMD GPUs are not officially supported. Do not rely on them for production use.

The `corridorkey doctor` command reports the active backend and any fallback
conditions on your specific hardware before processing begins.

---

## OS Version Requirements

| OS | Minimum Version |
|----|----------------|
| macOS | 14.0 (Sonoma) for official support; 13.x for best-effort |
| Windows | Windows 11 for officially supported paths |
| Windows 10 | Best-effort — not systematically tested |

---

## CLI vs OFX Plugin Support Scope

The CLI and OFX plugin share the same execution backends and hardware support
designations above. The OFX plugin additionally depends on the host application
(DaVinci Resolve) support defined in the host version table above.

The CLI does not have a host application dependency and can be used
independently of any NLE.
