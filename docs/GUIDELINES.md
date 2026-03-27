# Engineering Guidelines

This document defines the coding standards, API design rules, build system
requirements, testing strategy, and documentation standards for this project.
It is the reference for contributors and reviewers.

**See also:**
[ARCHITECTURE.md](ARCHITECTURE.md) — source structure and dependency rules |
[SPEC.md](SPEC.md) — product scope and support philosophy

---

## 1. Design Principles

### 1.1 Clean Architecture

The codebase follows layered architecture with a strict dependency direction:

```
CLI / OFX Plugin
  depends on
Public API (include/corridorkey/)
  depends on
Core Logic (src/ — inference, post-process, frame I/O)
  depends on
External Libraries (ONNX Runtime, OpenEXR, FFmpeg, MLX)
```

Rules:
- A lower layer never includes headers from a higher layer.
- External library types never appear in public headers in
  `include/corridorkey/`; they are wrapped in `src/`.
- No circular dependencies.

### 1.2 SOLID Principles

| Principle | Application |
|-----------|-------------|
| **S** — Single Responsibility | Each class and file has one reason to change. `ExrReader` reads EXR. `Despill` does despill. |
| **O** — Open/Closed | New execution providers or image formats should be addable without modifying existing core code. |
| **L** — Liskov Substitution | Any `ImageReader` implementation is interchangeable through the base interface. |
| **I** — Interface Segregation | Clients depend only on the interfaces they use. The CLI does not see internal inference details. |
| **D** — Dependency Inversion | `InferenceEngine` depends on abstract session interfaces, not directly on ONNX Runtime types. |

### 1.3 Reliability as a Core Tradeoff

This project explicitly prioritizes operational predictability, portable
packaging, and stable integration over raw throughput or broad hardware
coverage. Engineering decisions should reflect this: a robust, diagnostic-heavy
runtime that behaves consistently on validated hardware matters more than
claiming the widest possible compatibility.

### 1.4 Code Standards

- **Const correctness.** Everything that can be `const` is `const`.
- **RAII everywhere.** No raw `new`/`delete`. Use `std::unique_ptr`,
  `std::shared_ptr`, or stack allocation. FFmpeg and OpenEXR C resources
  are wrapped in RAII handles.
- **No raw owning pointers.** Non-owning observation via raw pointer or
  `std::span` is acceptable.
- **No global mutable state.** Configuration is passed explicitly through
  parameters or dependency injection.
- **No `std::exit` or `abort` in library code.** The library never terminates
  the process; it returns `Result<T>`.

### 1.5 Naming Conventions

| Element | Convention |
|---------|-----------|
| Functions, variables, files, namespaces | `snake_case` |
| Types (classes, structs, enums) | `PascalCase` |
| Constants and macros | `UPPER_SNAKE_CASE` |
| Private members | `m_` prefix |
| File extensions | `.hpp` for headers, `.cpp` for implementation |

No abbreviations except established domain terms: `fps`, `rgb`, `exr`, `io`.

---

## 2. API Design

The library serves multiple frontends. These rules keep the API stable and
frontend-agnostic.

### 2.1 Interface Stability

- **PIMPL pattern** for the main `Engine` class. Implementation details are
  hidden from public headers for ABI stability.
- **Minimal public headers.** `include/corridorkey/` exposes only what
  external consumers need.
- **Symbol visibility** is hidden by default. Only symbols marked with the
  `CORRIDORKEY_API` macro are exported.

### 2.2 Error Handling

- All operations that can fail use `std::expected<T, Error>` return types.
- Error types carry descriptive codes and messages usable in any UI context.
- The library does not throw exceptions across module boundaries where
  avoidable.

### 2.3 Execution and Threading

- Long-running operations accept a `ProgressCallback` for progress reporting
  and cancellation.
- The `Engine` class should be safe for concurrent read operations.
- The library does not print to stdout or stderr. Output is via callback or
  result type.

---

## 3. OFX-Specific Rules

### 3.1 Zero Exception Leakage

OpenFX is a pure C API. A C++ exception escaping any exported OFX function
will crash the host process immediately.

- All `extern "C"` entry points and `plugin_main_entry` must be wrapped in a
  top-level `try/catch(...)` block.
- Internal exceptions must be caught, logged, and translated into a safe
  OFX status code (e.g., `kOfxStatFailed`).

### 3.2 Zero Allocation in Per-Frame Paths

DaVinci Resolve schedules aggressive parallel render threads. Memory
fragmentation in per-frame code causes OFX instability.

- Do not allocate `std::vector<T>` or other heap containers inside per-pixel
  or per-frame functions.
- Use persistent `ScratchState` structs with pre-reserved capacity that are
  owned by the OFX instance lifecycle, not discarded at frame end.

---

## 4. Performance Standards

### 4.1 Memory and Data Locality

- `ImageBuffer` owns pixel data. `Image` (`std::span`) is used for
  processing. Do not use `std::vector<float>` for pixel data.
- No heap allocation in hot loops.
- All pixel buffers are 64-byte aligned (AVX-512 / NEON compatible).
- Process rows in Y-major order for sequential memory access.

### 4.2 Vectorization

- Avoid branches inside pixel loops. Use `std::clamp`, `std::min`,
  `std::max`.
- Keep loops simple enough for the compiler auto-vectorizer.
- Use lookup tables instead of `std::pow` or `std::exp` in hot paths.

### 4.3 Zero-Copy

- All processing functions take `Image` views (non-owning `std::span`).
- Use move semantics for `ImageBuffer` and `FrameResult` to transfer
  ownership without copying.

---

## 5. Build System

### 5.1 CMake

- **Target-based only.** No global commands (`include_directories`,
  `link_directories`, `add_definitions`). Use `target_include_directories`,
  `target_compile_definitions`, and `target_compile_options`.
- **`CMakePresets.json`** is the source of truth for build configurations.
  It defines `debug`, `release`, `release-lto`, and the macOS portable variants.
- On Windows, use `scripts/windows.ps1` as the canonical build and release
  entrypoint. The canonical release command is
  `scripts/windows.ps1 -Task release -Version X.Y.Z`. Lower-level Windows
  scripts are internal delegates for debugging the wrapper, not alternate
  release flows. If you invoke CMake directly, activate the MSVC developer
  environment first.
- Windows packaging may proceed with a partial model set, but the generated
  artifacts must include explicit inventory and validation reports. Missing
  models are reportable packaging state; invalid packaged models that are
  present remain release blockers.

### 5.2 Dependencies (vcpkg)

- **Manifest mode** via `vcpkg.json` with baseline pinned in
  `vcpkg-configuration.json`.
- Every dependency must have a `$comment` in `vcpkg.json` explaining why it
  exists.
- No new dependencies without that comment.

### 5.3 Compiler Settings

- **Strict warnings:** `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) or
  `/W4 /WX` (MSVC).
- **AddressSanitizer** enabled in the `debug` preset.

---

## 6. Static Analysis and Formatting

| Tool | Purpose | Config |
|------|---------|--------|
| clang-format 18+ | Code formatting | `.clang-format` |
| clang-tidy 18+ | Static analysis and naming enforcement | `.clang-tidy` |
| pre-commit | Local file hygiene and documentation consistency | `.pre-commit-config.yaml` |

The authoritative configuration for each tool is its config file at the
project root. Do not override tool settings in individual files.

---

## 7. Quality Gates

### 7.1 Gate Levels

```
Developer commits
        |
  pre-commit hook        Fast (< 30s)
  (staged files only)    - clang-format
                         - file hygiene
        |
  Developer pushes
        |
  pre-push hook          Thorough (< 5min)
  (full codebase)        - full release build
                         - all unit tests
                         - all integration tests
        |
  Repository CI          (when configured)
                         - cross-platform build and test
```

### 7.2 Pre-commit Hook

Managed via [pre-commit](https://pre-commit.com/) with configuration in
`.pre-commit-config.yaml`. Checks: formatting, trailing whitespace, YAML
syntax, large file detection, and documentation consistency.

### 7.3 Pre-push Hook

Shell script at `.githooks/pre-push`. Runs a full release build, unit tests,
and integration tests. All must pass before the push proceeds.

---

## 8. Testing Strategy

### 8.1 Pyramid

```
         E2E (few)           Full binary, real models, real hardware
       Integration           Multiple modules, real files, no GPU
     Unit (many)             Single function or class in isolation
```

### 8.2 Unit Tests

Single function or class, no external dependencies (no disk, no GPU, no model
files). Each test must complete in under one second. Framework: Catch2 v3.

Test: color math (sRGB/linear conversions, edge cases), despill/despeckle with
known input/output pairs, device detection logic, value type validation.

Do not unit test: thin wrappers around external libraries, trivial
constructors, private implementation details.

### 8.3 Regression Tests

Prevent specific bugs from reoccurring. Process: write a failing test that
reproduces the bug, fix the bug, keep the test permanently.

Naming: `test_regression_<issue_number>_<description>`.

Regression tests are never deleted.

### 8.4 Integration Tests

Multiple modules working together. May use real files from `tests/fixtures/`
(total fixture size should remain under 1 MB in the repository). No GPU.

Tests: file format round-trips (EXR, PNG, video), post-process chains, model
loading and session creation with a small test model.

### 8.5 End-to-End Tests

Full binary execution with real models, real files, real hardware. Slow and
hardware-dependent. Run locally on demand, nightly in CI if configured, and
mandatory before release.

### 8.6 Test Tags

| Tag | Scope | Schedule |
|-----|-------|----------|
| `[unit]` | No I/O, no GPU | Every commit |
| `[integration]` | May use disk, no GPU | Every push and CI |
| `[e2e]` | Real model and hardware | Nightly and before release |
| `[regression]` | Bug reproductions | Same as parent level |
| `[benchmark]` | Performance tracking | Nightly |

Domain tags (`[color]`, `[frameio]`, `[inference]`, `[device]`) are combined
with level tags.

---

## 9. Git Workflow

### 9.1 Branch Strategy

`main` is the integration branch. It should remain releasable. Merge only
after local quality gates pass and review is complete. Do not commit directly
to `main`.

Branch prefixes: `feat/`, `fix/`, `chore/`, `test/`, `refactor/`.

### 9.2 Commit Messages

[Conventional Commits](https://www.conventionalcommits.org/) format. Prefixes:
`feat`, `fix`, `test`, `refactor`, `chore`, `docs`, `perf`.

Subject line: short imperative sentence. Body (optional): explains why, not
what.

### 9.3 Developer Setup

Development setup instructions are in `CONTRIBUTING.md`.

---

## 10. Documentation Standards

### 10.1 Core Principle

Documentation contains definitions and decisions. It describes what the
project is, what it does, how it works, and why specific choices were made.

### 10.2 What Documentation Must Contain

- **Business context.** Every document and every major section starts with
  why it exists and what problem it solves, before technical details.
- **Definitions.** Concrete terms, types, interfaces, behaviors, constraints.
- **Current state.** What is built, how it works, how to use it.
- **Decisions.** Choices that were made and the reasons behind them.
- **Support claims tied to product tracks.** Compatibility statements must
  match packaged and validated product tracks in
  `help/SUPPORT_MATRIX.md`. Backend enums, probes, or core-only provider hooks
  are not support claims by themselves.

### 10.3 What Documentation Must Not Contain

- Speculation or unfounded plans. Use GitHub Issues for proposals.
- Historical notes. Git history and PRs serve that purpose.
- Dates, version stamps, or status markers in prose.
- Source code, except CLI usage examples in README that document the user
  interface.
- Emoji.
- Filler text ("this section will be expanded later"). If a section is not
  ready, it does not exist.
- Duplicated content. Each topic lives in one place.

### 10.4 Document Scope

| Document | Scope | Audience |
|----------|-------|----------|
| `README.md` | What this is, how to install, how to use | Users |
| `CONTRIBUTING.md` | Dev environment setup, PR process | Contributors |
| `docs/SPEC.md` | Product scope and support philosophy | Architects, developers |
| `docs/ARCHITECTURE.md` | Source structure and dependency rules | Developers |
| `docs/GUIDELINES.md` | Code standards, testing, build rules (this document) | Developers |
| `help/SUPPORT_MATRIX.md` | Support status by platform and hardware | Users, operators |
| `help/TROUBLESHOOTING.md` | Practical operational troubleshooting | Users, operators |
| `help/OFX_PANEL_GUIDE.md` | Practical OFX control guide for Resolve | Users, operators |
| `help/OFX_RESOLVE_TUTORIALS.md` | Step-by-step Resolve workflows | Users, operators |
| `docs/RELEASE_GUIDELINES.md` | Release build and packaging procedure | Release maintainers |
| `AGENTS.md` | Machine-readable rule summary for AI tools | AI tools |
| `CLAUDE.md` | Machine-readable rule summary for AI tools | AI tools |

### 10.5 Code Comments

Public API (`include/corridorkey/`): Doxygen-style comments on every public
function and class. States purpose, parameters, return value, preconditions,
and error conditions.

Internal code: comments explain why a decision was made — business reason,
performance trade-off, workaround — not what the code does.

Do not write:
- Comments that restate what the code does.
- Commented-out code. Delete it; use version control.
- TODO or FIXME. Open a GitHub Issue.

### 10.6 Writing Style

- Plain English, professional tone.
- Active voice, direct statements.
- Concrete and specific.
- Technical terms defined on first use.
- Consistent terminology throughout.

---

## 11. Security

- No network access in the core library. Model download is a separate CLI
  command, not implicit.
- Input validation at all system boundaries: file headers, resolution limits,
  parameter ranges.
- No shell command execution from the library.
- FFmpeg input is treated as untrusted. Use FFmpeg's built-in limits.
- Dependency audit via vcpkg vulnerability scanning in CI.
