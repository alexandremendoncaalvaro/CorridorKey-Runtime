# CorridorKey-Runtime

![CorridorKey OFX Plugin](./assets/ofx_example.gif)

Native AI keying runtime and OFX plugin for **DaVinci Resolve and Foundry Nuke**, built in collaboration with **Corridor Digital**.

CorridorKey-Runtime provides three product surfaces:

- **OFX plugin** for interactive keying inside DaVinci Resolve and Foundry Nuke
- **CLI** (`corridorkey`) for local processing, diagnostics, and automation
- **GUI** (Tauri-based desktop app) for users who prefer a graphical workflow over the CLI

The OFX plugin and the CLI ship in the same installer: the OFX bundle places `corridorkey.exe` next to `CorridorKey.ofx` and registers it on the system `PATH`, so the CLI is available after the OFX install. The GUI is distributed as a separate desktop installer that embeds its own copy of the runtime payload.

Current public builds support:

- **Windows (NVIDIA RTX)**
- **macOS (Apple Silicon)**

## Quick Start

- **Download the latest release:** [Releases](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases/latest)
- **Check supported platforms:** [Support Matrix](./help/SUPPORT_MATRIX.md)
- **Troubleshooting and setup help:** [User Help](./help/)
- **Watch a community tutorial:** [Tutorial by Heiko (@hethfilms)](https://youtu.be/VMcIk2jnQfg?si=IuitekQanKyDR4S-)

## What this project focuses on

- Native local execution
- Practical deployment in real editing workflows
- Support for **NVIDIA RTX** and **Apple Silicon**
- Stable surfaces for **DaVinci Resolve and Foundry Nuke users**, **CLI/integration workflows**, and **desktop GUI operators**

## Supported Surfaces

### OFX plugin for DaVinci Resolve and Foundry Nuke

Use CorridorKey directly inside DaVinci Resolve or Foundry Nuke through the OFX plugin. The plugin is host-agnostic and registers itself at the standard OpenFX bundle path, so any OFX 1.4-compliant host that scans that path picks it up.

### CLI (`corridorkey`)

The OFX installer ships the `corridorkey` CLI alongside the plugin and registers its directory on `PATH`. After installing the OFX bundle, `corridorkey` is callable from any terminal. Use it for:

- environment diagnostics
- scripted processing
- machine-readable JSON output
- automation and integration workflows

### GUI (Tauri desktop app)

A Tauri-based desktop application is available as a separate installer for users who prefer a graphical workflow over the CLI. The GUI embeds its own copy of the runtime payload, so it does not require the OFX bundle to be installed.

## Documentation

### User Help

- [OFX Panel Guide](./help/OFX_PANEL_GUIDE.md)
- [DaVinci Resolve Tutorials](./help/OFX_RESOLVE_TUTORIALS.md)
- [Support Matrix](./help/SUPPORT_MATRIX.md)
- [Troubleshooting](./help/TROUBLESHOOTING.md)

### Development Docs

- [Architecture](./docs/ARCHITECTURE.md)
- [Frontend](./docs/FRONTEND.md)
- [Guidelines](./docs/GUIDELINES.md)
- [Release Guidelines](./docs/RELEASE_GUIDELINES.md)
- [Spec](./docs/SPEC.md)
- [Contributing](./CONTRIBUTING.md)

## Installation

Pre-packaged releases are available on the
[Releases](https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/releases)
page. Download the package that matches your platform and product track.

For supported hardware configurations and explicit support status per platform,
see [Support Matrix](./help/SUPPORT_MATRIX.md).

### OFX Plugin - macOS (Apple Silicon)

1. Download the `.pkg` Apple Silicon installer.
2. Run the installer with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. The plugin uses the MLX-accelerated path
   automatically on M-series chips.

### OFX Plugin - Windows

1. Download the `.exe` installer for your hardware path:
   - **Windows RTX** - official Windows RTX installer for NVIDIA RTX 30 series
     and newer. This track ships the public FP16 quality ladder:
     **Draft (512)**, **High (1024)**, **Ultra (1536)**, and
     **Maximum (2048)**. `Auto` respects the safe VRAM ceiling for the active
     GPU tier, while manual fixed quality may attempt a higher packaged rung
     with explicit runtime fallback if it fails.
   - **DirectML package** - experimental Windows track for DirectX 12 GPUs
     outside the official RTX path. This track is not broadly validated across
     AMD, Intel, or RTX 20 series hardware and should only appear in releases
     when it is published intentionally.
2. Run the installer as Administrator with DaVinci Resolve closed.
3. Open DaVinci Resolve 20, go to the Color or Fusion page, and search for
   "CorridorKey" in the OpenFX Library.
4. Drag the node onto your clip. TensorRT RTX compilation on the first frame
   may take 10-30 seconds.

For plugin discovery issues, version mismatches, or unsupported hardware
behavior, see [Troubleshooting](./help/TROUBLESHOOTING.md).

### CLI

Download the portable runtime release for your platform.

- On macOS bundles and source builds, use `corridorkey`
- In the Windows portable runtime bundle, use `ck-engine.exe`

## CLI Usage

The examples below use the macOS and source-build command name `corridorkey`.
In the Windows portable runtime bundle, replace it with `ck-engine.exe`.

**Check hardware capability:**

```bash
corridorkey doctor
````

**Process a video with hardware-aware defaults:**

```bash
corridorkey process input.mp4 output.mp4
```

**Process with a specific preset:**

```bash
corridorkey process input.mp4 output.mp4 --preset max
```

**Process with an external Alpha Hint:**

```bash
corridorkey process input.mp4 output.mp4 --alpha-hint hint.mp4
```

Append `--json` to any command to receive NDJSON event streams for pipeline
integration.

## Community Tutorials

- **Tutorial by Heiko (@hethfilms):** practical walkthrough of the CorridorKey Resolve OFX plugin, including installation and real professional workflow usage. [Watch here](https://youtu.be/VMcIk2jnQfg?si=IuitekQanKyDR4S-)
- **Tutorial by Heiko (@hethfilms):** CORRIDORKEY - How to key difficult shots. [Watch here](https://www.youtube.com/watch?v=dAfeHmHFP9k)
## Building from Source

### Prerequisites

* C++20 compiler: Visual Studio 2022 (v17.4+), Apple Clang 15+, or GCC 12+
* CMake 3.28+
* Ninja
* vcpkg with `VCPKG_ROOT` set

### Build

```bash
git clone https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git
cd CorridorKey-Runtime
export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset release
cmake --build --preset release
```

On Windows, use `.\scripts\windows.ps1 -Task build -Preset release` for local
builds and `.\scripts\windows.ps1 -Task release -Version X.Y.Z` for official
Windows release packaging. That canonical release command emits the official
`Windows RTX` installer by default. Publish the experimental `DirectML` track
only when you request it explicitly with
`-Track dml` or `-Track all`.

Lower-level Windows scripts exist only as internal delegates for debugging the
wrapper itself. If you invoke CMake directly, activate the MSVC developer
environment first. Windows distribution artifacts include
`model_inventory.json` and `bundle_validation.json` when packaging succeeds
with a partial model set, so missing packaged models are explicit and do not
silently change runtime behavior.

For local Windows workflow, the canonical wrapper exposes three different
levels of operation:

* `.\scripts\windows.ps1 -Task build -Preset release`

  * build only
* `.\scripts\windows.ps1 -Task package-ofx -Version X.Y.Z -Track rtx`

  * package the `Windows RTX` installer from an already certified Windows RTX
    model set
* `.\scripts\windows.ps1 -Task certify-rtx-artifacts -Version X.Y.Z`

  * certify an already existing Windows RTX model set and write the artifact
    manifest without regenerating the `.onnx` files from the checkpoint
* `.\scripts\windows.ps1 -Task regen-rtx-release -Version X.Y.Z`

  * regenerate ONNX artifacts from the checkpoint, certify the RTX ladder,
    write the artifact manifest, and then package the `Windows RTX`
    installer

`package-ofx` for Windows RTX is intentionally strict. It no longer accepts a
raw `models\` folder by itself. The command requires a certified
`artifact_manifest.json` that matches the packaged RTX model and `*_ctx.onnx`
files exactly. If you only have stale or manually copied models, use
`certify-rtx-artifacts` or `regen-rtx-release` first.

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) for development setup and PR process.

## License

[CC BY-NC-SA 4.0](./LICENSE)

You may use this software to process commercial video. You may not repackage
or sell the software itself or offer it as a paid service.

## Acknowledgements

* **Original CorridorKey:** [github.com/nikopueringer/CorridorKey](https://github.com/nikopueringer/CorridorKey)
* **EZ-CorridorKey:** [github.com/edenaion/EZ-CorridorKey](https://github.com/edenaion/EZ-CorridorKey)
* **CorridorKey-Engine:** [github.com/99oblivius/CorridorKey-Engine](https://github.com/99oblivius/CorridorKey-Engine)
* **ONNX Runtime** by Microsoft
* **OpenEXR** by Academy Software Foundation
