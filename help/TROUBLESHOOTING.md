# Troubleshooting

This document covers practical steps for diagnosing and resolving the most
common operational issues with CorridorKey Runtime. It focuses on the OFX
plugin in DaVinci Resolve and the CLI.

For support status by platform, hardware, and Resolve version, see
[SUPPORT_MATRIX.md](SUPPORT_MATRIX.md).

Command note:
- Use `corridorkey` in source builds, macOS bundles, and Linux installs
- Use `ck-engine.exe` in the Windows portable runtime bundle

---

## Plugin Not Appearing in DaVinci Resolve

**Symptom:** The CorridorKey plugin is not visible in the OpenFX Library after
installation.

**Steps:**

1. Confirm DaVinci Resolve was closed before running the installer. The
   installer cannot register the plugin while Resolve holds an active OFX
   scan cache. Close Resolve, run the installer, then reopen Resolve.
2. Confirm the correct installer for your platform was used. The macOS `.dmg`
   installer and the Windows `.exe` installer are not interchangeable.
3. On Windows, confirm you chose the right Windows track:
   - `Windows RTX` for the official NVIDIA RTX 30 series and newer track
   - `DirectML` only for the experimental non-RTX Windows track
4. On Windows, confirm the installer was run as Administrator. The OFX plugin
   directory (`C:\Program Files\Common Files\OFX\Plugins`) requires elevated
   permissions to write.
5. On Linux, confirm the installer was run with `sudo`. The OFX plugin
   directory (`/usr/OFX/Plugins/`) requires root permissions to write.
   Resolve on Linux is Studio only; the free Resolve edition does not load
   OFX plugins on Linux.
6. Open DaVinci Resolve preferences and navigate to the OpenFX plug-ins section
   to verify that the standard OFX scan path is enabled.
7. Restart DaVinci Resolve to trigger a fresh OFX scan.
8. Check the Resolve log for OFX load errors:
   - macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`
   - Windows: `%AppData%\Blackmagic Design\DaVinci Resolve\logs\`
   - Linux: `~/.local/share/DaVinciResolve/logs/`

---

## Resolve Version Mismatch

**Symptom:** Plugin appears but fails to load, or appears as unsupported in
the OpenFX Library.

DaVinci Resolve 20 is the officially supported version. Resolve 18 has known
plugin discovery and loading issues that are not equivalent to Resolve 20
behavior.

**Steps:**

1. Run `corridorkey doctor` from the terminal to confirm the runtime is
   operating correctly outside of Resolve.
2. If using Resolve 18, note that plugin behavior is best-effort and known
   issues exist. Upgrading to Resolve 20 is the recommended resolution.
3. Review the Resolve log for the specific error code returned during OFX load.
   Error codes from the OFX load sequence are the most reliable diagnostic
   signal.

---

## Color Page Shows a Black Background Instead of Transparency

**Symptom:** On the DaVinci Resolve Color page, CorridorKey successfully
extracts the subject, but everywhere the key removed the background the
viewer shows **black** instead of transparency. The same project shows
real transparency in Fusion.

**Why this happens:** CorridorKey outputs a premultiplied RGBA image — the
alpha is present and correct in the output clip. Resolve's **Color page
viewer ignores the OFX alpha channel by default** and displays the
premultiplied RGB literally. Where alpha is zero, RGB is zero, which
shows as black. Fusion honors the alpha natively because Fusion is a
compositing graph. This is how every OFX keyer behaves inside Resolve
(Keylight, Primatte, etc.) — not a CorridorKey-specific limitation.

**Steps — route the alpha through the node tree so the Color page viewer
uses it for transparency:**

1. Open the **Color** page and select the node with CorridorKey applied.
2. In the **Node Graph**, **right-click the CorridorKey node** and choose
   **Add Alpha Output**. A blue triangle output pin appears on the right
   side of the node.
3. The node graph itself already has an **Alpha Output** triangle on the
   far right (next to the RGB output). Drag a connection from the blue
   triangle you just added on the CorridorKey node to that graph-level
   Alpha Output.
4. The Color page viewer now respects the alpha. Transparent regions show
   through to whatever is below this clip on the timeline, or to the
   timeline background if nothing is underneath.

**Steps — inspect the key itself on the Color page without routing alpha:**

1. On the CorridorKey panel, change **Output Mode** to **Matte Only**.
   The viewer then displays the generated matte as grayscale, which is
   visible in the Color page without any node-graph changes.
2. Switch back to **Processed** for the keyed composite after you have
   validated the matte.

The plugin-side output is the same RGBA data in both pages; the
difference is purely in how the Color page viewer consumes alpha. Route
the alpha output once per node and the Color workflow matches Fusion.

---

## Guide Source Shows "Rough Fallback"

**Symptom:** The runtime panel shows `Guide Source: Rough Fallback`.

The current Resolve OFX path prefers a connected **Alpha Hint** input, but it
can continue by generating a rough fallback guide when no readable hint is
available. That keeps the node running, but the result is a degraded guidance
path rather than the preferred workflow.

**Steps:**

1. On the Color page, right-click the CorridorKey node and choose
   **Add OFX Input**.
2. Connect a rough matte from a Qualifier, 3D Keyer, garbage matte, or another
   matte source into the new green input.
3. In Fusion, connect the guide matte to CorridorKey's secondary
   **Alpha Hint** input.
4. Prefer a true alpha or single-channel matte. If you feed an RGB image, the
   plugin reads the red channel.
5. Check the runtime panel again. Confirm **Guide Source** switches from
   `Rough Fallback` to `External Alpha Hint`.
6. If the plugin still cannot read the connected hint, inspect the source clip
   format and confirm the matte is actually present in the expected channel.

---

## Hardware Path Not Activating

**Symptom:** `corridorkey doctor` reports a fallback to CPU, an explicit
backend failure, or a different backend than expected.

**Steps:**

1. Run `corridorkey doctor` to see the detected hardware, selected backend,
   and any fallback or failure conditions.
2. Confirm the correct package was installed for your hardware:
   - Apple Silicon: the `.pkg` installer uses the MLX path automatically.
   - NVIDIA RTX 30 series or newer: the Windows `Windows RTX` installer is the
     official path.
   - Other Windows GPUs: the Windows `DirectML` installer is the only shipped
     experimental Windows fallback track.
3. On Windows, confirm that the required DirectX 12 runtime is present. Run
   `dxdiag` and verify the DirectX version.
4. On Windows with NVIDIA hardware, confirm that the NVIDIA driver is current.
   TensorRT RTX execution requires a driver version compatible with the
   packaged runtime. Outdated drivers are a common cause of initialization
   failure.
5. Treat RTX 20 series, AMD GPUs, and Intel GPUs as experimental Windows
   paths. Known errors exist in practice and these families are not currently
   validated as official product tracks.

---

## Wrong Package or Backend Expectation

**Symptom:** Processing is slower than expected, or `doctor` reports a
different backend than anticipated.

Each packaged track has a different support level:

- `Windows RTX` is the official Windows RTX package for NVIDIA RTX 30 series
  and newer through ONNX Runtime TensorRT RTX EP.
- The `DirectML` package is an experimental Windows track. It must not be
  treated as proof of official support across AMD, Intel, or RTX 20 series
  hardware.
- Apple Silicon packages use MLX as the primary path.

Installing the wrong package for your hardware can produce either explicit
backend failure or a fallback path, depending on the surface:

- CLI and tolerant automation workflows may fall back to the ONNX CPU path.
- The OFX plugin may fail explicitly instead of silently switching to CPU when
  the requested interactive GPU path is unsupported.

Verify which package is installed and whether it matches your hardware by
running `corridorkey doctor`.

---

## Missing Model Artifacts or Artifact Mismatch

**Symptom:** `corridorkey doctor` reports missing model artifacts, or
processing fails immediately with a model load error.

The runtime expects curated model artifacts under `models/`. In source
checkouts those files live in the repository. In packaged releases they are
staged next to the runtime bundle or inside the plugin bundle.

**Steps:**

1. Verify that the expected files are present under `models/` in your source
   checkout or packaged runtime.
2. In packaged releases, keep the `models` directory with the runtime payload.
   Do not move it away from the executable or bundle contents.
3. Run `corridorkey doctor` or `ck-engine.exe doctor` to confirm the runtime
   sees the artifacts at the resolved path.
4. If the artifacts were copied manually, re-stage them from a known-good
   release bundle instead of mixing files from different builds.
5. Do not modify or repack model artifacts. The runtime validates artifact
   integrity at load time.

---

## Backend Requirements

CorridorKey requires a supported GPU on every render path:

- Windows: NVIDIA RTX 30 series or newer with TensorRT-RTX.
- macOS: Apple Silicon with MLX.

CPU rendering has been retired alongside INT8 ONNX. Requesting CPU no longer
resolves to a packaged artifact; the renderer surfaces a "no supported render
backend" failure rather than downgrading to a quality the bundle cannot ship.
If you previously relied on the `Allow CPU Fallback` toggle, the parameter no
longer exists in the OFX panel and saved values are ignored on load.

To confirm the active backend and the reason for any failure, run:

```bash
corridorkey doctor
```

The output reports the active backend and the reason for any fallback or
failure.

---

## First-Run Warmup and TensorRT RTX Compilation

**Symptom:** The first frame processed after installation takes 10-30 seconds
before output appears.

This is expected behavior on TensorRT RTX paths. TensorRT RTX compiles and
optimizes the model for your specific GPU during the first run. The compiled
engine is cached on disk. Subsequent runs use the cached engine and do not
incur this delay.

The compilation cache is tied to the GPU model, driver version, and TensorRT
RTX runtime version. A driver update or runtime update may invalidate the
cache and trigger a recompile on the next run.

---

## Logs and Bug Report Guidance

Before filing a bug report, collect the following:

1. **`corridorkey doctor` output.** Run `corridorkey doctor` and capture the
   full output. This identifies hardware, backend, model artifact status, and
   any fallback or failure conditions.
2. **Runtime log.** The runtime writes a log file during operation:
   - macOS: `~/Library/Logs/CorridorKey/`
   - Windows: `%LOCALAPPDATA%\CorridorKey\Logs\`
   - Linux: `/tmp/corridorkey-logs/`
3. **Resolve OFX log** (if the issue is plugin-specific):
   - macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`
   - Windows: `%AppData%\Blackmagic Design\DaVinci Resolve\logs\`
   - Linux: `~/.local/share/DaVinciResolve/logs/`
4. **System information.** OS version, GPU model, driver version, Resolve
   version, and which installer package was used.

File the bug report at:
[github.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues)

Include the `doctor` output, the relevant log excerpt, and system information.
Bug reports without this information will be asked to provide it before
investigation begins.

If you are reporting an issue on an experimental path, such as Windows
DirectML or NVIDIA RTX 20 series, note that resolution is not guaranteed. See
[SUPPORT_MATRIX.md](SUPPORT_MATRIX.md) for the support designation of your
hardware.
