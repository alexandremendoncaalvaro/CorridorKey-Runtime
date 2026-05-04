# AI Agent Rules

This file is read automatically by supported coding agents at the start of
each session. It contains the non-negotiable rules for this repository.

## Project Identity

- **Name:** CorridorKey Runtime
- **Language:** C++20 (no modules)
- **Build:** CMake 3.28+ with vcpkg manifest mode and CMake Presets
- **License:** CC BY-NC-SA 4.0

## Structural Rules (enforced — see docs/ARCHITECTURE.md for details)

- Public headers go in `include/corridorkey/` — this is the external API
- Use **PIMPL Pattern** for main classes such as `Engine` to ensure ABI stability
- Use **Symbol Visibility (hidden by default)** — only export via `CORRIDORKEY_API`
- **Zero-Copy Performance:** use `std::span` via `Image` for processing and
  `ImageBuffer` for ownership. Do not use `std::vector<float>` for large pixel data
- **SIMD Alignment:** ensure 64-byte alignment for all image allocations
- Implementation goes in `src/` subdirectories by domain:
  - `src/app/` — application orchestration, runtime contracts, diagnostics, OFX service
  - `src/cli/` — CLI only (main + arg parsing, no business logic)
  - `src/common/` — shared internal utilities (STL-only, no external deps)
  - `src/core/` — inference engine, device detection, ONNX Runtime wrapper
  - `src/frame_io/` — EXR, PNG, video I/O
  - `src/gui/` — Tauri desktop UI and bridge code
  - `src/plugins/ofx/` — OFX plugin integration over the app-layer service
  - `src/post_process/` — pure pixel math (color utils, despill, despeckle)
- Tests go in `tests/unit/`, `tests/integration/`, `tests/e2e/`, or `tests/regression/`
- External library types (`OrtSession`, `Imf::*`, `AVFrame`, etc.) never appear in
  public headers — they are wrapped in `src/`
- Do not create new top-level directories or new `src/` subdirectories without
  updating docs/ARCHITECTURE.md

## Code Standards (see docs/GUIDELINES.md for details)

- `snake_case` for functions, variables, files
- `PascalCase` for types (classes, structs, enums)
- `m_` prefix for private members
- `UPPER_SNAKE_CASE` for constants
- `.hpp` for headers, `.cpp` for implementation — no `.h` files
- Const correctness everywhere
- RAII for all resources — no raw `new`/`delete`
- Error handling via `std::expected` or `std::optional`, not exceptions for
  expected failures
- Max cognitive complexity per function: 15
- Max ~200 lines per `.cpp`, ~100 lines per `.hpp` (guideline, not hard rule)
- Prefer early returns over nested if/else
- No abbreviations in names except standard domain terms: `rgb`, `exr`, `fps`, `io`

## Testing

- Framework: Catch2 v3
- Tags: `[unit]`, `[integration]`, `[e2e]`, `[regression]`
- Bug fixes must include a regression test
- Unit tests: no I/O, no GPU, < 1 second each
- Test file naming: `test_<module>.cpp`

## Build & Infrastructure

- Use `CMakePresets.json` as the source of truth for build configurations
- Always use **vcpkg Manifest Mode** with `vcpkg-configuration.json` for baseline pinning
- Strict warnings: `-Wall -Wextra -Wpedantic -Werror` or the MSVC equivalent
- Enable **AddressSanitizer (ASAN)** in Debug presets
- Keep build behavior target-based in CMake. Do not use global include, link,
  or warning configuration when a target-scoped setting is available

## Operational Rules

- Use feature branches and PRs. Do not work directly on `main`
- `scripts/windows.ps1` is the only Windows entrypoint. Every Windows
  build, package, certification, and release runs through it via
  `-Task build | prepare-rtx | certify-rtx-artifacts | package-ofx |
  package-runtime | release | regen-rtx-release | sync-version`. Never
  call `scripts/build.ps1`, `scripts/prepare_windows_rtx_release.ps1`,
  `scripts/release_pipeline_windows.ps1`, or any other sub-script
  directly; they are internal delegates and skip the version metadata
  sync, track resolution, and validation the wrapper applies.
- When any prerequisite is missing (TensorRT-RTX SDK at
  `vendor/TensorRT-RTX/`, `vcpkg` at `VCPKG_ROOT`, `uv`, CUDA Toolkit
  12.8), fix the prerequisite. Do not route around the canonical
  pipeline.
- The only supported repo-local Windows runtime roots are
  `vendor/onnxruntime-windows-rtx` and `vendor/onnxruntime-windows-dml`
- Do not use `vendor/onnxruntime-universal` or a globally installed ONNX Runtime
  as a Windows build or release dependency
- Do not create git worktrees that shadow `vendor/`. `git worktree
  remove --force` has followed Windows junctions into `vendor/` and
  erased the real curated runtimes. If a second working copy is needed,
  stage its own curated runtimes instead of sharing them.
- Changes to the render hot path (`src/plugins/ofx/`,
  `src/core/inference_session.cpp`, `src/core/engine.cpp`,
  `src/core/gpu_prep.cpp`, `src/core/gpu_resize.cpp`,
  `src/post_process/`) must be measured against the
  `phase_8_gpu_prepare` baseline in `docs/OPTIMIZATION_MEASUREMENTS.md`
  via `scripts/run_corpus.sh` + `scripts/compare_benchmarks.py`. Reject
  the change when `avg_latency_ms` or `ort_run` regresses by more than
  10%.
- Windows packaging may proceed with a partial model set, but missing packaged
  models must be surfaced in generated inventory and validation reports.
  Missing models are reportable packaging state; invalid packaged models that
  are present still block the release flow.
- Keep `AGENTS.md` and `CLAUDE.md` identical. If one changes, change the other
  in the same diff

## Commit Style

- Conventional Commits: `feat:`, `fix:`, `test:`, `refactor:`, `chore:`,
  `docs:`, `perf:`
- Do not commit to `main` directly
- **Every commit must carry a `Signed-off-by:` trailer.** The repository
  enforces the Developer Certificate of Origin (see `CONTRIBUTING.md`
  section "Developer Certificate of Origin (DCO)") via a status check
  that blocks merge on any commit lacking the trailer. Use
  `git commit --signoff` (or `-s`) so the trailer is added at commit
  time. Commits authored through the Claude Code agent are no exception
  — passing `--signoff` from the start avoids the "rebase + force-push
  to add sign-off retroactively" remediation that rewrites every commit
  hash on the branch (and detaches any tag previously published from the
  branch ancestry it once shared). Past remediation required an admin
  merge because unsigned commits had accumulated; preserving the published
  Windows prerelease tag's commit hash precluded the rewrite path.

## Documentation Rules (see docs/GUIDELINES.md section 10 for details)

- Documentation contains **definitions and decisions**, not speculation,
  history, or unfounded plans
- No dates, version stamps, `DRAFT` markers, or changelogs in docs
- No emoji anywhere: not in docs, code, or commits
- Every document starts with business context (why) before technical details
- Each document has one scope. Do not duplicate content across documents
- Code is the primary documentation of behavior. Comments explain **why**,
  not **what**
- No commented-out code and no `TODO`/`FIXME` in source. Use GitHub Issues
- Tests are living documentation of behavior

## What NOT to Do

- Do not use `std::vector` for image data; use `ImageBuffer`
- Do not use expensive math functions (`std::pow`, `std::exp`) inside hot pixel loops
- Do not add files to root unless they are project-level config or documentation
- Do not put business logic in `src/cli/`
- Do not leak external library types into `include/corridorkey/`
- Do not create documentation files unless explicitly asked
- Do not bypass hooks with `--no-verify`
- Do not commit model files (`.onnx`, `.pth`) unless the repository already tracks them
- Do not add dependencies without a `$comment` in `vcpkg.json`
- Do not put source code in documentation files
- Do not write comments that restate what the code does
- Do not use `std::exit` or `abort` in the library; return `Result<T>` instead
- Do not use global state or static variables in the library
