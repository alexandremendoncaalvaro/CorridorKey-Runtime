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

## Foundry Nuke - Host Version Support

| Nuke Version | OFX Plugin Support |
|--------------|-------------------|
| Nuke 17.0v2 (Windows RTX) | Best-effort - validated via headless smoke and manual UAT |
| Nuke 16.x, 15.x, 14.x | Best-effort - shares the OFX 1.4 surface, not systematically validated |
| Nuke 13.x and earlier | Unsupported |

The plugin uses standard OpenFX 1.4 properties and suites that are part of
every modern Nuke release; there are no Nuke-specific code paths in the
render hot path. Help labels and the tutorial button adapt to the active
host based on the `kOfxPropName` string the host advertises.

Cache cleanup on plugin update is handled automatically by the Windows
installer when Nuke is detected. The installer clears
`%LOCALAPPDATA%\Temp\nuke\ofxplugincache\ofxplugincache_Nuke<ver>-64.xml`
so the freshly staged bundle is rescanned on the next Nuke launch.

Designation escalates to Officially supported once a licensed-runner
headless smoke (`CORRIDORKEY_RUN_NUKE_E2E=1`) is gating CI and the track
has accumulated a clean run history.

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
  newer. It packages the complete FP16 ladder through `2048px`. INT8 ONNX and
  CPU rendering have been retired: FP16 on RTX is the only quality and only
  backend the installer ships.
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

- `CorridorKey_OFX_vX.Y.Z_Linux_RTX.tar.gz` - universal portable archive
  with `install.sh` and `uninstall.sh` helpers.
- `CorridorKey_OFX_vX.Y.Z_Linux_RTX.deb` - Debian package for Ubuntu
  22.04 LTS and Ubuntu 24.04 LTS.
- `CorridorKey_OFX_vX.Y.Z_Linux_RTX.rpm` - RPM package for Rocky Linux 9
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

CPU rendering has been retired. The OFX plugin and CLI both require a
supported GPU (NVIDIA RTX 30 series or newer on Windows; Apple Silicon with
MLX on macOS). Requesting `Backend::CPU` no longer resolves to a packaged
artifact and surfaces a "no supported render backend" failure rather than
falling back to a quality the renderer cannot ship.

---

## Tauri Desktop GUI Support Scope

The Tauri desktop GUI is distributed as an independent installer that embeds
its own copy of the runtime payload. It does not require an OFX host and does
not require the OFX bundle to be present on the system.

The GUI inherits the same backend and hardware support designations as the
OFX plugin and CLI on the same machine. A configuration officially supported
for the OFX plugin is officially supported for the GUI; experimental tracks
remain experimental in the GUI.

---

## Host Application Coverage

The current officially supported host applications are DaVinci Resolve and
Foundry Nuke through the OFX plugin. The CLI is a host-independent surface.
The Tauri GUI is an additional host-independent surface.

Other editing applications, including Adobe Premiere Pro and Adobe After
Effects, are not currently supported. They are not OFX hosts and require
their own SDK integrations that have not been built.

---

## Screen Color Model Variants

The runtime ships two model variants distinguished by the screen color the
model was trained on. Each variant is distributed as an independent pack and
each variant has its own per-resolution support status.

### Green model variant

The canonical CorridorKey model, trained on green screen plates.

| Resolution | TensorRT RTX EP | MLX (Apple Silicon) | Status |
|------------|-----------------|---------------------|--------|
| 512px | Validated | Validated | Officially supported |
| 1024px | Validated | Validated | Officially supported |
| 1536px | Validated | Validated | Officially supported |
| 2048px | Validated | Validated | Officially supported |

Green packs are part of every official Windows RTX and macOS Apple Silicon
installer.

### Blue model variant

A dedicated CorridorKey variant trained on blue screen plates. Blue runs on
two distinct backends, each with its own resolution coverage:

- **TensorRT RTX EP** is the historical Windows RTX backend. Engine compilation
  on the blue checkpoint succeeds at 512px and fails at 1024px and above due
  to FP16 numerical instability in the blue weights at those tensor sizes.
- **Torch-TensorRT** is the second Windows RTX backend, tracked for blue
  resolutions where TensorRT RTX EP cannot serve the artifact. The blue model
  compiles cleanly under Torch-TRT at 1024px (FP16) and at 1536px and 2048px
  (FP32, since FP16 still overflows at those sizes). Engines are
  `hardware_compatible=True` so a single artifact runs on Ampere and newer
  RTX families.

#### Windows RTX coverage

| Resolution | Backend | Status |
|------------|---------|--------|
| 512px | TensorRT RTX EP, FP16 | Officially supported - dedicated blue model |
| 1024px | Torch-TensorRT, FP16 | Officially supported - dedicated blue model |
| 1536px | Torch-TensorRT, FP32 | Officially supported - dedicated blue model |
| 2048px | not currently packaged | When the dedicated artifact is not present, `corridorkey doctor`, the OFX runtime status surface, and the GUI report the gap and prompt the user to switch to a packaged blue resolution or to the green model at 2048px. The runtime does not silently substitute a different model |

#### macOS Apple Silicon coverage

| Resolution | Backend | Status |
|------------|---------|--------|
| 512px | MLX (canonicalization through green) | Officially supported |
| 1024px | MLX (canonicalization through green) | Officially supported |
| 1536px | MLX (canonicalization through green) | Officially supported |
| 2048px | MLX (canonicalization through green) | Officially supported |

There is no dedicated blue MLX artifact today. On Apple Silicon, blue plates
are handled by canonicalizing the input into the green domain, running the
green MLX model, and restoring the result. This is automatic and reported in
`corridorkey doctor`.

#### Missing artifact UX

When a packaged dedicated blue artifact is required by the user's selection
and the corresponding pack is not installed, the runtime surfaces the gap
explicitly through the surface in use:

- **CLI:** non-zero exit with a message naming the missing pack and the
  resolutions that are currently servable.
- **OFX plugin (Resolve, Nuke):** runtime status field reports
  `missing_dedicated_blue_pack` with the requested resolution and a
  human-readable suggestion. The render call returns an explicit error rather
  than producing a fallback render.
- **Tauri GUI:** the resolution selector surfaces the missing tier inline and
  links to the model pack download flow.

This behavior is product policy: blue users explicitly choose a quality tier,
and a silent substitution would violate that contract. Users may switch to a
packaged blue resolution, switch to the green model, or install the missing
pack from the canonical Hugging Face source.

### Pack distribution

Model packs are selectable. The installer or first-run flow lets the user
choose green only, blue only, or both. Packs not selected at install time can
be added later through the same flow. Missing packs surface as missing
artifacts in `corridorkey doctor`, with the canonical Hugging Face download
location attached.
