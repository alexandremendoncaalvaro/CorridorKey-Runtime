# CorridorKey Runtime

CorridorKey Runtime is a production-oriented native engine for CorridorKey
inference. It packages model execution, diagnostics, validated model catalogs,
and stable machine-readable contracts into a distributable C++ runtime for real
hardware.

This repository is not trying to reproduce a generic Python workflow in another
language. It exists to make CorridorKey usable as a native, predictable, and
integrable engine that can be installed, benchmarked, automated, and embedded
without rebuilding the surrounding stack each time.

The delivery sequence is explicit:

- **macOS first** to close runtime quality, diagnostics, and portable
  distribution on Apple Silicon
- **Windows RTX next** as the next product track for predictable consumer GPU
  deployment
- **GUI, sidecar, and embedded integrations after that**, all consuming the
  same library-first runtime contracts

## Who This Is For

- **Technical local operators** who want to run CorridorKey without Python,
  virtual environments, or ad hoc setup.
- **Windows RTX users** who care about predictable installation, provider
  behavior, and reproducible performance on consumer GPUs.
- **Integrators** who want a native engine they can embed into an application,
  plugin, sidecar, or pipeline without re-implementing business logic.

## Why This Exists vs. Original Workflow

- **Native execution:** single-binary workflow with a C++ core and no Python
  runtime dependency.
- **Low-friction distribution:** portable CLI packaging, validated model
  catalogs, and explicit hardware tracks.
- **Operational predictability:** `doctor`, `benchmark`, JSON/NDJSON contracts,
  fallback reasons, and stage timings make runtime behavior inspectable.
- **Library-first integration:** CLI, future GUI, sidecar, and embedded use
  cases all sit on the same engine instead of copying behavior across surfaces.

## Features

- **macOS production track:** CoreML-first execution on Apple Silicon with
  mandatory CPU fallback.
- **Windows RTX next:** TensorRT RTX is the planned primary Windows product
  track after macOS release gates are closed.
- **Operational tooling:** `doctor`, `benchmark`, `models`, `presets`, and
  `process --json` expose stable machine-readable diagnostics.
- **Validated model catalog:** `int8_512` and `int8_768` are the packaged macOS
  defaults in the current phase.
- **Measured runtime behavior:** stage-level timings cover engine creation,
  warmup, inference, tiling, decode, encode, and full-job execution.
- **Library + CLI:** the runtime is reusable as a C++ library and exposed
  through a thin CLI contract.

## Quick Start

Validated runtime path today: **macOS 14+ on Apple Silicon**.

Windows RTX is the next product track already documented in the roadmap, but it
is **not** the current release gate yet. Linux remains architecture-ready and
deferred until macOS and Windows RTX are both clear.

### Prerequisites

- C++20 compiler (GCC 12+, Clang 16+, MSVC 17.4+)
- [CMake 3.28+](https://cmake.org/download/)
- [Ninja](https://ninja-build.org/) build system
- [vcpkg](https://github.com/microsoft/vcpkg) package manager

### 1. Install vcpkg

Skip this step if `VCPKG_ROOT` is already set.

<details>
<summary>Linux / macOS</summary>

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT="$HOME/vcpkg"
```

</details>

<details>
<summary>Windows</summary>

```powershell
git clone https://github.com/microsoft/vcpkg.git %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
```

</details>

### 2. Build

```bash
git clone https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime.git
cd CorridorKey-Runtime
cmake --preset release
cmake --build build/release --parallel
```

### 3. Add to PATH (optional)

```bash
export PATH="$(pwd)/build/release/src/cli:$PATH"
```

If you prefer a symlink:

```bash
sudo ln -s "$(pwd)/build/release/src/cli/corridorkey" /usr/local/bin/corridorkey
```

## Usage

If `corridorkey` is not on your `PATH`, replace it with
`./build/release/src/cli/corridorkey`.

**Download model files**:

```bash
corridorkey download --variant int8
```

**Inspect runtime capabilities and hardware recommendation**:

```bash
corridorkey info --json
corridorkey doctor --json --model models/corridorkey_int8_768.onnx
```

**Inspect validated models and presets**:

```bash
corridorkey models --json
corridorkey presets --json
```

**Run a synthetic benchmark with timings**:

```bash
corridorkey benchmark --json --model models/corridorkey_int8_512.onnx --device cpu
```

**Run a real-workload benchmark**:

```bash
corridorkey benchmark --json --input input.mp4 --output benchmark_output.mp4 --model models/corridorkey_int8_768.onnx
```

**Process a single video with NDJSON events**:

```bash
corridorkey process --json --input input.mp4 --alpha-hint hint.mp4 --output output.mp4 --model models/corridorkey_int8_768.onnx
```

**Process a directory of frames**:

```bash
corridorkey process --input ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/ --model models/corridorkey_int8_768.onnx
```

**Use tiled inference for larger inputs**:

```bash
corridorkey process --input input_4k.mp4 --output output_4k.mp4 --model models/corridorkey_int8_768.onnx --tiled
```

### Output

```text
Output/
  Matte/       # Alpha channel — EXR 16-bit linear
  FG/          # Foreground straight color — EXR 16-bit linear
  Processed/   # Premultiplied RGBA — EXR 16-bit linear
  Comp/        # Preview on checkerboard — PNG 8-bit sRGB
```

## AlphaHint Strategy

Alpha hints are part of the product contract, not an afterthought.

- The runtime accepts hints as a single frame, an image sequence, or a video
  input.
- If no hint is provided, the runtime falls back to its internal rough matte
  generation path.
- External hints remain the preferred path for higher-quality and more
  controlled results.
- Rough matte fallback exists for smoke tests, low-friction usage, and simpler
  cases, not as a replacement for dedicated hint workflows.

## Machine-Readable Interfaces

- `info`, `doctor`, `benchmark`, `models`, and `presets` return a single JSON
  document when `--json` is present.
- `benchmark --json` supports:
  - `synthetic` for controlled throughput and latency checks without external
    I/O
  - `workload` for end-to-end measurement on a real image sequence or video
- `benchmark --json` reports backend selection, structured fallback details,
  and `stage_timings`.
- `process --json` emits NDJSON events:
  - `job_started`
  - `backend_selected`
  - `progress`
  - `warning`
  - `artifact_written`
  - `completed`
  - `failed`
  - `cancelled`
- Terminal `process --json` events include aggregated stage timings for the
  full job.

## Operational Promise

- **Consistent hardware tiers:** on the current macOS release track, `auto`
  maps to `512` on 8 GB-class Macs and `768` on 16 GB-class Macs.
- **Observable execution:** warmup, inference, tiling, decode, encode, and
  full-job timings are inspectable without an external profiler.
- **Curated provider support:** the product validates a few provider tracks
  deeply instead of claiming broad parity across every ONNX Runtime backend.
- **Stable automation contract:** JSON and NDJSON outputs are part of the
  product surface, not debug-only output.

## Delivery Sequence

1. **macOS production runtime:** CoreML-first execution, CPU fallback, tiling,
   corpus validation, and portable CLI bundle.
2. **Windows RTX next:** TensorRT RTX-focused Windows track with predictable
   installation, diagnostics, and validated tiers.
3. **Integration surfaces:** sidecar, GUI, plugin, and pipeline consumers over
   the same library-first contracts.
4. **Broader platform expansion:** Linux and secondary providers after macOS
   and Windows RTX are both well-defined.

## Platform Status

| Platform | Primary path | Status | Notes |
|----------|--------------|--------|-------|
| macOS 14+ Apple Silicon | CoreML + CPU fallback | Current release gate | `auto` prefers CoreML and falls back to CPU with a structured reason |
| Windows 11 + NVIDIA RTX | TensorRT RTX + CPU fallback | Next product track | Planned next delivery track after macOS gates |
| Windows 11 general | CPU; DirectML/WinML secondary | Exploratory | Not treated as the primary Windows product path |
| Linux | Architecture-ready later | Deferred | Not a current productized track |

## Documentation

- [Technical Specification](docs/SPEC.md) — Product direction, contracts,
  technical decisions, delivery phases
- [Architecture](docs/ARCHITECTURE.md) — Library-first structure and dependency
  rules
- [Frontend](docs/FRONTEND.md) — GUI constraints and sidecar contract
- [Engineering Guidelines](docs/GUIDELINES.md) — Code standards, testing
  strategy, linting, git hooks

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards,
and how to submit changes.

## License

[CC BY-NC-SA 4.0](LICENSE) — Same license as the original CorridorKey project.

You may use this software to process commercial video. You may not repackage
and sell the software itself or offer it as a paid service. See
[LICENSE](LICENSE) for full terms.

## Credits

- [CorridorKey](https://github.com/nikopueringer/CorridorKey) by Niko Pueringer
  / Corridor Digital — the original neural green screen keyer
- [ONNX Runtime](https://onnxruntime.ai/) by Microsoft — model execution and
  execution-provider infrastructure
- [OpenEXR](https://openexr.com/) by Academy Software Foundation — VFX image
  format
