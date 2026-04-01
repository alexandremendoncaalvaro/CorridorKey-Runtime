# Ship `Torch-TensorRT` C++ Directly on the Current Branch and Cut `v0.9.0`

## Summary
- Implement the work on the current branch `codex/feat-engine-selector-max-perf`, keeping the already-committed Hugging Face model migration intact.
- Turn the existing engine selector into a real comparison:
  - `Auto` and `ORT TensorRT` remain the official Windows RTX path
  - `Torch-TensorRT` becomes a real experimental C++ engine, not a placeholder
- Keep zero-friction delivery: no Python dependency for end users, no sidecar Python runtime in the installer.
- Absorb the best proven optimizations from `C:\Dev\CorridorKey-Engine` and `C:\Dev\CorridorKey-Cloud` where they map cleanly to the C++ runtime and do not risk visible quality loss.
- Finish with a publish-ready local release `v0.9.0`, full validation, senior-style review, and release notes text ready for GitHub.

## Implementation Changes
### Runtime and engine architecture
- Keep the current `ExecutionEngine` contract and make the public comparison truthful:
  - `auto`
  - `ort-tensorrt`
  - `torch-tensorrt`
- Remove the public `max-performance` pseudo-engine from UI/CLI/docs and fold its safe ORT improvements into the official ORT path.
- Add a real Torch session path in `src/core` parallel to the ORT session path, without exposing `libtorch` or Torch-TensorRT types in public headers.
- Keep engine identity in:
  - session cache keys
  - OFX runtime protocol
  - diagnostics
  - benchmark JSON
  - bundle validation
- Policy:
  - `Auto` chooses official ORT
  - explicit `torch-tensorrt` never silently falls back to ORT
  - precompiled Torch artifacts may fall back to local recompilation in C++, then persist the new compiled cache

### Torch-TensorRT C++ engine
- Use the officially documented C++ deploy path:
  - generate TorchScript source modules offline in tooling
  - load and compile them in C++ with `libtorch` + `libtorchtrt`
  - save and reload compiled Torch/TensorRT artifacts for reuse
- Use standard TensorRT for the Torch engine v1, not TensorRT-RTX.
- Build the Torch engine around fixed-shape FP16 per rung:
  - `512`
  - `1024`
  - `1536`
  - `2048`
- Compile policy for production:
  - FP16 explicit typing
  - fixed shape per rung
  - high optimization level
  - persistent engine/timing cache
  - compile on engine switch, quality switch, install-time validation, or first prepare
  - never compile in the frame hot path
- Package both kinds of Torch artifacts through Hugging Face:
  - TorchScript source artifact per rung
  - precompiled compiled-module/engine artifact per rung as fast path
  - compatibility metadata
- Runtime load order for Torch:
  1. try packaged precompiled artifact
  2. validate against GPU/runtime compatibility
  3. if incompatible, recompile locally in C++
  4. persist the local compiled result and use it on later runs

### Performance work to absorb from the forks
- Promote the current ORT branch improvements into the official ORT path:
  - buffer reuse
  - pinned host memory
  - I/O binding where stable
  - fused CPU loops
  - session pooling and engine-aware cache identity
- Bring over the proven fork ideas that translate cleanly to C++:
  - explicit warmup separate from measurement
  - persistent device buffers for fixed shapes
  - pinned D2H buffers and non-blocking copies on a dedicated stream
  - compile/cache persistence
  - pooled per-rung sessions
  - graph-friendly fixed-shape execution
- Torch-specific runtime optimizations:
  - GPU preprocess
  - GPU postprocess where visually safe
  - single final copy back to host
  - CUDA Graphs only after warmup and only for fixed-shape steady-state runs
  - invalidate graphs on any parameter change that alters graph structure or shape
- Post-process optimizations allowed only if they pass visual parity:
  - GPU resize in/out
  - input packing/normalization on device
  - `source_passthrough`, premultiply, composite, and despill on device
  - morphology with repeated small kernels only if output remains visually equivalent
  - gaussian edge softening retained as quality reference
- Do not ship in this phase:
  - runtime Python
  - INT8
  - model-architecture changes like token routing or refiner experiments
  - any optimization that changes matte quality without documented parity checks

### Build, dependencies, and packaging
- Add the required C++ Torch runtime dependencies in the Windows build system with target-scoped CMake only.
- Vendor and package the Windows Torch runtime bundle alongside the current curated ORT runtime, with explicit validation that the Torch runtime is complete if the engine is exposed.
- Update the HF fetch pipeline to support Torch artifacts in the same model flow already adopted on this branch.
- Update canonical scripts only:
  - `scripts/fetch_models.ps1`
  - `scripts/windows.ps1` delegates if needed
  - release/package/validation scripts
- Update docs that must remain in sync:
  - `docs/ARCHITECTURE.md`
  - `docs/GUIDELINES.md` only if structural/build rules truly change
  - `README.md`
  - `help/SUPPORT_MATRIX.md`
  - `AGENTS.md`
  - `CLAUDE.md`
- Keep documentation factual:
  - ORT is the official Windows RTX engine
  - Torch-TensorRT is experimental in the same installer
  - no broad support claims beyond the support matrix

## Validation, Review, and Release
### Required checks
- Run, in order, the canonical validation stack required by the repo:
  - `pre-commit run --all-files`
  - `powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Preset debug`
  - `ctest --preset unit --output-on-failure`
  - `ctest -L integration --output-on-failure`
  - `ctest -L regression --output-on-failure`
  - `ctest -L e2e --output-on-failure`
  - any relevant Python exporter tests for Torch artifact generation
  - Windows release flow via `scripts/windows.ps1 -Task release -Version 0.9.0 -Track rtx`
- Add regression tests for:
  - engine switch resets session correctly
  - Torch explicit selection does not silently fall back
  - precompiled artifact incompatibility triggers local recompile
  - cache identity is isolated by engine
  - OFX parameter changes do not trigger unexpected recompiles
  - HF artifact flow handles Torch artifacts correctly

### Senior review pass before release
- Review the implementation as if it were a junior PR and fix any findings before packaging.
- Review criteria:
  - no public headers leak Torch/TensorRT external types
  - no business logic in `src/cli`
  - no new top-level directories
  - no per-frame heap churn in OFX hot paths
  - no `TODO`/`FIXME`
  - `AGENTS.md` and `CLAUDE.md` remain identical
  - docs describe real shipped behavior only
  - warnings remain clean under strict flags
  - release claims match packaged and validated tracks
- If the documented Windows C++ Torch path cannot be built and validated cleanly in this repo, do not ship a fake selector; instead keep the branch honest and stop before release packaging.

### Release output
- Version bump: `0.9.0`
- Local installer to produce:
  - `CorridorKey_Resolve_v0.9.0_Windows_RTX_Installer.exe`
- Release artifacts and reports to verify:
  - installer
  - zip
  - `bundle_validation.json`
  - `doctor_report.json`
  - `model_inventory.json`
  - release notes markdown prepared in `dist/`
- GitHub release title:
  - `CorridorKey Resolve OFX v0.9.0 (Windows)`

### Release text to prepare
```markdown
## Overview
This release adds a real Windows RTX engine comparison path inside the OFX plugin and CLI. The official ONNX Runtime TensorRT path remains the default, and an experimental direct C++ Torch-TensorRT engine is now available for side-by-side performance testing on supported NVIDIA RTX hardware.

## Changelog
### Added
- Experimental `Torch-TensorRT` execution engine for the Windows RTX OFX plugin and CLI
- Engine-aware diagnostics, cache reporting, and benchmark output
- Hugging Face artifact flow for Torch runtime assets

### Changed
- Folded the previous Windows RTX hot-path improvements into the official ORT TensorRT engine
- Improved Windows RTX session reuse, cache behavior, and fixed-shape execution paths
- Expanded release validation to verify packaged Torch runtime completeness when the experimental engine is included

### Fixed
- Engine comparisons now distinguish implementation path, backend, artifact, and cache state correctly
- Engine-specific session invalidation and runtime reporting are now consistent across OFX and CLI

## Assets & Downloads

### Windows
- **NVIDIA RTX 30 Series or newer:** Download `CorridorKey_Resolve_v0.9.0_Windows_RTX_Installer.exe`.

## Installation Instructions
1. Close DaVinci Resolve if it is running.
2. Run the downloaded installer.
3. The installer automatically overwrites the previous version.
4. Launch DaVinci Resolve and load the plugin from the OpenFX Library.

## Uninstallation
To remove the plugin, go to **Windows Settings > Apps > Installed apps**, search for "CorridorKey Resolve OFX", and click Uninstall.
```

## Assumptions and defaults
- Work stays on `codex/feat-engine-selector-max-perf`.
- The Hugging Face migration already on the branch is the source of truth for model/artifact distribution.
- `Torch-TensorRT` ships as experimental, but fully functional and benchmarkable, in the same Windows RTX installer.
- End users install only the plugin; no separate Python or Torch setup is required.
- Performance is prioritized for steady-state throughput on `RTX30xx+`, with longer first-run compile accepted.
- Visual quality is a hard gate; no optimization survives if it causes perceptible matte/composite degradation.

## References
- [Torch-TensorRT C++ API](https://docs.pytorch.org/TensorRT/getting_started/getting_started_with_cpp_api.html)
- [Torch-TensorRT Performance Tuning Guide](https://docs.pytorch.org/TensorRT/user_guide/performance_tuning.html)
- [Torch-TensorRT Runtime / deployment](https://docs.pytorch.org/TensorRT/user_guide/runtime.html)
- [Torch-TensorRT on Windows](https://docs.pytorch.org/TensorRT/getting_started/getting_started_with_windows.html)
- [NVIDIA TensorRT support matrix](https://docs.nvidia.com/deeplearning/tensorrt/10.13.3/getting-started/support-matrix.html)
- [NVIDIA TensorRT advanced compatibility guidance](https://docs.nvidia.com/deeplearning/tensorrt/10.13.2/inference-library/advanced.html)
