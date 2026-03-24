# Troubleshooting

This document covers practical steps for diagnosing and resolving the most
common operational issues with CorridorKey Runtime. It focuses on the OFX
plugin in DaVinci Resolve and the CLI.

For support status by platform, hardware, and Resolve version, see
[SUPPORT_MATRIX.md](SUPPORT_MATRIX.md).

---

## Plugin Not Appearing in DaVinci Resolve

**Symptom:** The CorridorKey plugin is not visible in the OpenFX Library after
installation.

**Steps:**

1. Confirm DaVinci Resolve was closed before running the installer. The
   installer cannot register the plugin while Resolve holds an active OFX
   scan cache. Close Resolve, run the installer, then reopen Resolve.

2. Confirm the correct installer for your platform was used. The macOS `.pkg`
   installer and the Windows `.exe` installer are not interchangeable.

3. On Windows, confirm the installer was run as Administrator. The OFX plugin
   directory (`C:\Program Files\Common Files\OFX\Plugins`) requires elevated
   permissions to write.

4. Open DaVinci Resolve preferences and navigate to the OpenFX plug-ins section
   to verify that the standard OFX scan path is enabled.

5. Restart DaVinci Resolve to trigger a fresh OFX scan.

6. Check the Resolve log for OFX load errors:
   - macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`
   - Windows: `%AppData%\Blackmagic Design\DaVinci Resolve\logs\`

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

## Status Says "Waiting for Alpha Hint connection."

**Symptom:** The runtime panel says `Waiting for Alpha Hint connection.` and the
result looks like a pass-through image instead of a keyed result.

The current Resolve OFX path does not run inference until the **Alpha Hint**
input is connected.

**Steps:**

1. On the Color page, right-click the CorridorKey node and choose
   **Add OFX Input**.

2. Connect a rough matte from a Qualifier, 3D Keyer, garbage matte, or another
   matte source into the new green input.

3. In Fusion, connect the guide matte to CorridorKey's secondary
   **Alpha Hint** input.

4. Prefer a true alpha or single-channel matte. If you feed an RGB image, the
   plugin reads the red channel.

5. Check the runtime panel again. Do not evaluate quality or matte controls
   until the waiting message is gone.

---

## Hardware Path Not Activating

**Symptom:** `corridorkey doctor` reports a fallback to CPU, or the wrong
backend is selected.

**Steps:**

1. Run `corridorkey doctor` to see the detected hardware and selected backend.
   The output identifies which backend is active and any conditions that caused
   a fallback.

2. Confirm the correct package was installed for your hardware:
   - Apple Silicon: the `.pkg` installer includes MLX support automatically.
   - NVIDIA Ampere/Ada: the TensorRT package is required.
   - Intel iGPU or other DirectX 12 hardware: the DirectML package is required.

3. On Windows, confirm that the required DirectX 12 runtime is present. Run
   `dxdiag` and verify the DirectX version.

4. On Windows with NVIDIA hardware, confirm that the NVIDIA driver is current.
   TensorRT execution requires a driver version compatible with the packaged
   TensorRT runtime. Outdated drivers are a common cause of TensorRT
   initialization failure.

5. RTX 20 series (Turing) and AMD GPUs are experimental. Known errors exist in
   practice on these hardware families. If `doctor` reports errors on these
   paths, CPU fallback is the current recommended workaround.

---

## Wrong Package or Backend Expectation

**Symptom:** Processing is slower than expected, or `doctor` reports a
different backend than anticipated.

Each Windows package ships a specific backend:
- The TensorRT package targets NVIDIA Ampere and newer.
- The DirectML package targets Intel iGPU and other DirectX 12 hardware.

Installing the TensorRT package on hardware without an NVIDIA Ampere GPU will
cause TensorRT initialization to fail. The runtime will fall back to ONNX CPU
execution, which is correct behavior, but slower than intended.

Verify which package is installed and whether it matches your hardware by
running `corridorkey doctor`.

---

## Missing Model Pack or Artifact Mismatch

**Symptom:** `corridorkey doctor` reports missing model artifacts, or
processing fails immediately with a model load error.

The model pack is distributed separately from the runtime binary. It is not
included in the repository.

**Steps:**

1. Download the model pack from the Releases page. Each release specifies
   which model pack version it requires.

2. Place the model pack in the expected location. Run `corridorkey doctor` to
   confirm it is detected correctly.

3. If the model pack was updated independently of the runtime, verify that the
   model pack version matches the runtime version. An incompatible artifact
   will produce a schema mismatch error at load time.

4. Do not modify or repack model artifacts. The runtime validates artifact
   integrity at load time.

---

## Fallback Behavior

When the preferred backend fails or is unavailable, the runtime falls back to
ONNX CPU execution. This is intentional. Fallback is always logged.

CPU fallback is significantly slower than GPU execution. It is suitable for
validation but not recommended for production throughput.

To confirm fallback is occurring and understand the reason, run:

```bash
corridorkey doctor
```

The output reports the active backend and the reason for any fallback.

---

## First-Run Warmup and TensorRT Compilation

**Symptom:** The first frame processed after installation takes 10–30 seconds
before output appears.

This is expected behavior on TensorRT paths. TensorRT compiles and optimizes
the model for your specific GPU during the first run. The compiled engine is
cached on disk. Subsequent runs use the cached engine and do not incur this
delay.

The compilation cache is tied to the GPU model, driver version, and TensorRT
version. A driver update or runtime update may invalidate the cache and
trigger a recompile on the next run.

---

## Logs and Bug Report Guidance

Before filing a bug report, collect the following:

1. **`corridorkey doctor` output.** Run `corridorkey doctor` and capture the
   full output. This identifies hardware, backend, model artifact status, and
   any fallback conditions.

2. **Runtime log.** The runtime writes a log file during operation:
   - macOS: `~/Library/Logs/CorridorKey/`
   - Windows: `%LOCALAPPDATA%\CorridorKey\Logs\`

3. **Resolve OFX log** (if the issue is plugin-specific):
   - macOS: `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`
   - Windows: `%AppData%\Blackmagic Design\DaVinci Resolve\logs\`

4. **System information.** OS version, GPU model, driver version, Resolve
   version, and which installer package was used.

File the bug report at:
[github.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues)

Include the `doctor` output, the relevant log excerpt, and system information.
Bug reports without this information will be asked to provide it before
investigation begins.

If you are reporting an issue on an experimental path (RTX 20 series, AMD),
note that resolution is not guaranteed. See
[SUPPORT_MATRIX.md](SUPPORT_MATRIX.md) for the support designation of your
hardware.
