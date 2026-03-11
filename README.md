# CorridorKey Runtime

CorridorKey Runtime is a production-oriented native engine for CorridorKey
inference. It packages model execution, diagnostics, validated model catalogs,
and stable machine-readable contracts into a distributable C++ runtime for real
hardware.

The runtime contract is shared across platform tracks. The model artifact is
not. This product is designed to use the backend and converted model pack that
best fit each hardware target instead of forcing one artifact shape across
Apple Silicon, RTX consumer GPUs, and CPU fallback.

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

- **Platform-specific model packs:** the runtime stays library-first and stable
  while each hardware track can use a different converted artifact.
- **macOS Apple Silicon track:** MLX is now the default operational path for
  Apple model packs, while ONNX remains the CPU-compatible compatibility and
  diagnostics baseline.
- **Windows RTX next:** TensorRT RTX is the planned primary Windows product
  track, with its own RTX-oriented artifact strategy after macOS release gates
  are closed.
- **Operational tooling:** `doctor`, `benchmark`, `models`, `presets`, and
  `process --json` expose stable machine-readable diagnostics.
- **Validated model catalog:** the packaged macOS set now centers on
  `corridorkey_mlx.safetensors` for Apple acceleration and `int8_512` for CPU
  smoke and fallback checks.
- **Measured runtime behavior:** stage-level timings cover engine creation,
  warmup, inference, tiling, decode, encode, and full-job execution.
- **Library + CLI:** the runtime is reusable as a C++ library and exposed
  through a thin CLI contract.

## Quick Start

Validated runtime path today: **macOS 14+ on Apple Silicon** with MLX-first
execution for Apple model packs, CPU fallback for ONNX baselines, structured
diagnostics, and a curated model catalog.

Windows RTX is the next product track already documented in the roadmap, but it
is **not** the current release gate yet. Linux remains architecture-ready and
deferred until macOS and Windows RTX are both clear.

## Model Packs and Platform Tracks

The product boundary is the runtime and its contracts. Model packs are curated
per platform track.

- **Baseline ONNX packs:** `corridorkey_int8_512.onnx` remains the packaged CPU
  smoke and fallback baseline, while `corridorkey_int8_768.onnx` stays
  available as a manual CPU compatibility baseline for 16 GB-class Macs.
- **macOS Apple Silicon accelerated pack:** treated as an Apple-specific model
  pack, not as a requirement to reuse the same ONNX artifact shipped for other
  targets. The approved Mac evaluation paths are `MLX` and direct `Core ML`
  conversion from the source checkpoint.
- **Windows RTX pack:** remains an ONNX-derived path aimed at `TensorRT RTX`
  with a separate packaging and cache strategy.

This means the runtime stays unified while acceleration can diverge where the
hardware demands it.

## Current Apple Silicon Finding

The current Mac conclusion is no longer hypothetical:

- The **short-path accelerated artifact that is already validated in the
  upstream Apple Silicon stack is `corridorkey_mlx.safetensors`**, not the
  current ONNX `int8` packs used for CPU fallback and diagnostics here.
- On a real 4K greenscreen frame from
  `assets/video_samples/mixkit-girl-dancing-with-her-earphones-on-a-green-background-28306-4k.mp4`
  with a controlled coarse hint, the official `corridorkey-mlx` engine running
  `tiled_1024` completed in **52354.714 ms** with **3487.092 MB** peak memory.
- The current runtime on the **same frame and same hint** using
  `corridorkey_int8_768.onnx`, `cpu`, `--tiled`, and `--batch-size 2`
  completed in **110488.172 ms**. That puts the validated MLX path at roughly
  **2.11x faster** than the current CPU baseline on this machine.
- The current `ONNX -> ORT CoreML EP` path remains a **diagnostic baseline**
  only. It still partitions onto CPU with the current ONNX artifacts and is
  not the preferred Mac acceleration track.
- The packaged Mac runtime now ships the official `corridorkey_mlx.safetensors`
  weights together with bundled `.mlxfn` bridge exports. The pack stays the
  public Apple artifact; the bridge exports are the execution layer used by the
  runtime today.

The immediate Mac implementation path is now:

1. Run Apple model packs through **MLX by default** on macOS.
2. Keep `ONNX/CoreML EP` for compatibility diagnostics and CPU fallback
   investigation only.
3. Evaluate **direct `PyTorch -> Core ML` conversion** in parallel as the
   candidate final distributable Apple artifact once the runtime surface is
   proven on MLX.

### Preparing The MLX Model Pack

The repository now includes a helper to materialize the Apple model pack and
the bridge exports required by the runtime bundle:

```bash
source .venv-macos-mlx/bin/activate
python scripts/prepare_mlx_model_pack.py \
  --output-dir models \
  --tag v1.0.0
```

By default this prepares:

- `corridorkey_mlx.safetensors`
- `corridorkey_mlx_bridge_512.mlxfn`
- `corridorkey_mlx_bridge_1024.mlxfn`

For local builds, CMake now tries these MLX discovery paths in order:

1. `CORRIDORKEY_MLX_CMAKE_DIR`
2. `CORRIDORKEY_MLX_PYTHON`
3. active `VIRTUAL_ENV`
4. repository-local `.venv-macos-mlx`
5. default `Python3_EXECUTABLE`

After configuring and building with MLX available, `corridorkey doctor --json`
should report:

- `mlx.probe_available = true`
- `mlx.primary_pack_ready = true`
- `mlx.bridge_ready = true` when the bundled bridge exports are present
- `mlx.backend_integrated = true` when the runtime can execute the packaged MLX path
- `summary.apple_acceleration_probe_ready = true`
- `summary.apple_acceleration_backend_integrated = true` when the packaged MLX
  path is linked and importable

The current native execution path is labeled `mlx_pack_with_bridge_exports` in
`doctor --json`. That is the current shipping runtime path for Apple Silicon.

On this machine, the current runtime benchmark at synthetic `512` reports:

- `MLX`: `avg_latency_ms = 601.189`
- `CPU int8_512`: `avg_latency_ms = 1050.283`

That puts the current MLX runtime path at roughly `1.75x` the
throughput of the CPU baseline at the same synthetic resolution.

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

**Download ONNX baseline packs**:

```bash
corridorkey download --variant int8
```

**Prepare the Apple Silicon MLX pack**:

```bash
python scripts/prepare_mlx_model_pack.py --output-dir models
```

**Inspect runtime capabilities and hardware recommendation**:

```bash
corridorkey info --json
corridorkey doctor --json --model models/corridorkey_mlx.safetensors
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

**Run a real-workload benchmark on the Apple path**:

```bash
corridorkey benchmark --json --input input.mp4 --output benchmark_output.mp4 --model models/corridorkey_mlx.safetensors --device mlx
```

**Process a single video with NDJSON events on Apple Silicon**:

```bash
corridorkey process --json --input input.mp4 --alpha-hint hint.mp4 --output output.mp4 --model models/corridorkey_mlx.safetensors --tiled
```

**Process a directory of frames with the CPU baseline**:

```bash
corridorkey process --input ./Input/ --alpha-hint ./AlphaHint/ --output ./Output/ --model models/corridorkey_int8_512.onnx
```

**Use tiled inference for larger inputs on the Apple path**:

```bash
corridorkey process --input input_4k.mp4 --output output_4k.mp4 --model models/corridorkey_mlx.safetensors --tiled
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
  is model-aware. Apple MLX packs resolve to the MLX backend at the engine's
  recommended resolution, while ONNX baselines stay on CPU with conservative
  per-tier resolution defaults.
- **Observable execution:** warmup, inference, tiling, decode, encode, and
  full-job timings are inspectable without an external profiler.
- **Curated provider support:** the product validates a few provider tracks
  deeply instead of claiming broad parity across every ONNX Runtime backend or
  pretending one model artifact should fit every platform equally well.
- **Stable automation contract:** JSON and NDJSON outputs are part of the
  product surface, not debug-only output.

## Delivery Sequence

1. **macOS production runtime:** choose and validate the Apple-specific
   accelerated model-pack path, keep CPU fallback predictable, close tiling,
   corpus validation, and portable CLI bundle.
2. **Windows RTX next:** TensorRT RTX-focused Windows track with predictable
   installation, diagnostics, validated tiers, and its own RTX-oriented model
   pack.
3. **Integration surfaces:** sidecar, GUI, plugin, and pipeline consumers over
   the same library-first contracts.
4. **Broader platform expansion:** Linux and secondary providers after macOS
   and Windows RTX are both well-defined.

## Platform Status

| Platform | Primary path | Status | Notes |
|----------|--------------|--------|-------|
| macOS 14+ Apple Silicon | MLX-first Apple model packs; CPU ONNX fallback and diagnostics | Current release gate | Runtime contracts are fixed; accelerated Mac artifacts are not assumed to match Windows |
| Windows 11 + NVIDIA RTX | TensorRT RTX + CPU fallback | Next product track | Planned next delivery track after macOS gates, with a separate RTX-oriented model pack |
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
