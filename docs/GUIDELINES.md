# Engineering Guidelines

> **See also:**
> [ARCHITECTURE.md](ARCHITECTURE.md) — project structure, dependency layers |
> [SPEC.md](SPEC.md) — technical specification and implementation phases

---

## 1. Design Principles

### 1.1 Clean Architecture

The codebase follows a layered architecture with strict dependency direction:

```
CLI (main.cpp)
  depends on
Public API (include/corridorkey/)
  depends on
Core Logic (src/ — inference, post-process, frame I/O)
  depends on
External Libraries (ONNX Runtime, OpenEXR, FFmpeg)
```

Rules:
- **Dependency inversion:** Core logic depends on abstractions, not concrete
  implementations. External libraries are wrapped behind interfaces.
- **No upward dependencies:** A lower layer never includes headers from a
  higher layer.
- **No circular dependencies:** Enforced by include-what-you-use and build
  structure.

### 1.2 SOLID Principles

| Principle | Application |
|-----------|-------------|
| **S** — Single Responsibility | Each class/file has one reason to change. `ExrReader` reads EXR. `Despill` does despill. No god objects. |
| **O** — Open/Closed | New execution providers or image formats are added without modifying existing code — via interfaces and registration. |
| **L** — Liskov Substitution | Any `ImageReader` implementation (EXR, PNG, Video) is interchangeable through the base interface without surprises. |
| **I** — Interface Segregation | Clients depend only on the interfaces they use. The CLI does not see internal inference details. |
| **D** — Dependency Inversion | `InferenceEngine` depends on an abstract `ISession`, not directly on ONNX Runtime types. External libs are behind wrappers. |

### 1.3 Object Calisthenics (adapted for C++)

Guiding constraints, not absolute laws. They push toward cleaner code. When a
rule makes the code worse, document why it is being broken.

| Rule | C++ Adaptation | Enforced by |
|------|---------------|-------------|
| One level of indentation per function | Max 2 levels of nesting. Extract helper functions instead. | clang-tidy: cognitive-complexity (threshold: 15) |
| Don't use else | Prefer early returns, guard clauses, std::optional/std::expected. Else is permitted for simple if/else pairs. | Code review |
| Wrap primitives | Domain types: `Resolution`, `DespillStrength` instead of raw int/float where semantics matter. Do not over-wrap trivially obvious parameters. | Code review |
| First-class collections | Wrap collections that carry behavior: `FrameSequence` instead of `std::vector<Frame>` with external logic. Plain vectors are fine for simple data. | Code review |
| One dot per line | Avoid long chains. Intermediate variables with descriptive names. Method chaining in builders is acceptable. | Code review |
| Don't abbreviate | Full names: `alpha_hint`, not `ah`. `frame_count`, not `fc`. Standard abbreviations are acceptable: `fps`, `rgb`, `exr`, `io`. | clang-tidy: identifier-naming |
| Keep classes small | Target: < 200 lines per .cpp, < 100 lines per .hpp. Split when it grows. | CI check |
| Limit instance variables | Prefer small structs. When a class has > 5 members, consider splitting. | Code review |
| No getters/setters | Prefer immutable value types or methods that express behavior. Trivial POD structs with public members are fine. | Code review |

### 1.4 Integration as a Structural Goal

The runtime is designed as a reusable foundation for multiple surfaces (CLI,
GUI, sidecar). Logic must reside in the library core, never in the frontend
wrapper. All features must be accessible via the library API.

### 1.5 Commitment to Reliability (Central Tradeoff)

This project explicitly privileges **operational predictability, portable
packaging, and stable integration** over the raw speed of research
experimentation. Engineering decisions prioritize a robust, diagnostic-heavy
runtime that "just works" for the end user, even when this requires more
disciplined development cycles and higher engineering overhead.

### 1.6 Code Standards

- **Const correctness:** Everything that can be const, is const.
- **RAII everywhere:** No raw `new`/`delete`. Use `std::unique_ptr`,
  `std::shared_ptr`, stack allocation. FFmpeg/OpenEXR C resources wrapped
  in RAII handles.
- **No raw owning pointers.** Non-owning observation via raw pointer or
  `std::span` is fine.
- **No global mutable state.** Configuration passed explicitly through
  parameters or dependency injection.
- **Naming conventions:** `snake_case` for functions/variables/namespaces/files,
  `PascalCase` for types, `UPPER_SNAKE_CASE` for constants and macros,
  `m_` prefix for private members. All names in English.

---

## 2. API Design

The library exists to serve multiple frontends (CLI today, GUI or plugins in
the future). These rules ensure the API remains stable and frontend-agnostic.

### 2.1 Interface Stability

- **PIMPL pattern** for the main `Engine` class. This hides implementation
  details from public headers, ensuring ABI stability.
- **Minimal public headers.** Only expose what external consumers need. Keep
  `include/corridorkey/` lean.
- **Symbol visibility** hidden by default. Only symbols marked with the
  `CORRIDORKEY_API` macro are exported.

### 2.2 Error Handling

- **No `std::exit` or `abort` in the library.** The library never terminates
  the process.
- **Explicit results** via `std::expected<T, Error>` for all operations that
  can fail (I/O, model loading, inference).
- Error types carry descriptive codes and messages usable in any UI.

### 2.3 Execution and Threading

- Long-running operations accept a `ProgressCallback` for progress reporting
  and cancellation.
- The `Engine` class is thread-safe for concurrent read operations.
- The library does not print to stdout/stderr. Logging is via callback or
  standard interface that the consumer can redirect.

---

## 3. Performance Standards

The C++ runtime exists because Python is too slow and too memory-hungry.
These rules protect the performance advantage.

### 3.1 Memory and Data Locality

- **`ImageBuffer` for ownership, `Image` (std::span) for processing.** Do not
  use `std::vector<float>` for pixel data.
- **Zero heap allocations in hot loops.** Per-frame and per-pixel code must
  not allocate.
- **64-byte aligned allocation** for all pixel buffers (AVX-512 compatible).
- **Row-major processing** (outer Y, inner X) for sequential memory access.

### 3.2 Vectorization

- Avoid branches inside pixel loops. Use `std::clamp`, `std::min`, `std::max`.
- Keep loops simple enough for the compiler auto-vectorizer.
- Prefer lookup tables over expensive math (`std::pow`, `std::exp`) in hot paths.

### 3.3 Zero-Copy

- All processing functions take `Image` views (non-owning `std::span`).
- Use move semantics for `ImageBuffer` and `FrameResult` to transfer ownership
  without copying.

---

## 4. Build System

### 4.1 CMake

- **Target-based only.** No global commands (`include_directories`,
  `link_directories`, `add_definitions`). All configuration via
  `target_include_directories`, `target_compile_definitions`,
  `target_compile_options`.
- **CMakePresets.json** is the source of truth for build configurations.
  Contains presets for `debug`, `release`, and `ci`.
- **Local Windows Build Environment:** To build on Windows, you MUST run CMake from within the **x64 Native Tools Command Prompt for VS**. This ensures the C++ standard libraries (like `<string_view>`, `<type_traits>`, etc.) and Windows SDK are natively exposed to Ninja. A normal PowerShell or CMD window is insufficient.
- A developer runs `cmake --preset debug` from the Native Tools Prompt and gets a working build.

### 4.2 Dependencies (vcpkg)

- **Manifest mode** via `vcpkg.json` with locked baseline in
  `vcpkg-configuration.json`.
- Every dependency has a `$comment` explaining why it exists.
- Prefer header-only libraries for small utilities (vendored in `vendor/`).
- Dependencies updated quarterly. Security patches applied immediately.

### 4.3 Compiler Settings

- **Strict warnings:** `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) or
  `/W4 /WX` (MSVC).
- **AddressSanitizer** enabled in the `debug` preset.

---

## 5. Static Analysis and Formatting

### 5.1 Tools

| Tool | Purpose | Config file |
|------|---------|-------------|
| **clang-format 18+** | Code formatting | `.clang-format` |
| **clang-tidy 18+** | Static analysis, style enforcement | `.clang-tidy` |
| **cppcheck** | Undefined behavior, leaks, portability | optional local/CI gate |
| **include-what-you-use** | Header minimality | CMake integration |

### 5.2 clang-format

Based on LLVM style. Key decisions: 4 spaces, 100 column limit, attach braces,
no single-line if/loops, includes regrouped (project first, third-party second,
standard last), pointer/reference aligned left.

The authoritative configuration is `.clang-format` at the project root.

### 5.3 clang-tidy

Enables check groups: bugprone, cert, cppcoreguidelines, misc, modernize,
performance, readability, concurrency. Critical checks promoted to errors.
Naming enforcement matches project conventions.

The authoritative configuration is `.clang-tidy` at the project root.

### 5.4 cppcheck

Enables warning, style, performance, and portability checks. Run it as an
explicit quality gate in local verification scripts or CI workflows.

---

## 6. Quality Gates

Nothing reaches the repository without passing automated checks.

### 6.1 Gate Levels

```
Developer commits
        |
  pre-commit hook        Fast (< 30s)
  (staged files only)    - clang-format
                         - file hygiene
        |
  git commit created
        |
  Developer pushes
        |
  pre-push hook          Thorough (< 5min)
  (full codebase)        - full release build
                         - all unit tests
                         - all integration tests
        |
  push to remote
        |
  CI pipeline            Optional (if configured)
  (GitHub Actions)       - cross-platform build and test
```

### 6.2 Pre-commit Hook

Managed via [pre-commit](https://pre-commit.com/). Configuration in
`.pre-commit-config.yaml`. Checks: formatting, trailing whitespace,
YAML syntax, and large file detection.

### 6.3 Pre-push Hook

Shell script at `.githooks/pre-push`. Runs: full release build, unit tests,
and integration tests. All must pass.

### 6.4 CI Pipeline

When configured in `.github/workflows/`, CI should build and test on supported
platforms while preserving the same quality baseline used locally.

---

## 7. Testing Strategy

### 7.1 Pyramid

```
         E2E (few)           Full binary, real models, real files
       Integration           Multiple modules together
     Unit (many)             Single function/class in isolation
```

The goal is **high-value coverage across all layers**, not 100% unit test
coverage.

### 7.2 Unit Tests

Single function or class, no external dependencies (no disk, no GPU, no model
files). Must complete in under 1 second each. Framework: Catch2 v3.

What to test: color math (sRGB/linear conversions, edge cases, round-trips),
despill/despeckle with known input/output pairs, device detection logic,
resolution selection, value type validation.

What NOT to unit test: thin wrappers around external libraries, trivial
constructors, private implementation details.

### 7.3 Regression Tests

Prevent specific bugs from reoccurring. Process: write a failing test that
reproduces the bug, then fix the bug. Naming:
`test_regression_<issue_number>_<description>`.

Regression tests are never deleted.

### 7.4 Integration Tests

Multiple modules working together. May use real files from `tests/fixtures/`
(< 1MB total in repo). No GPU. Tests: file format round-trips (EXR, PNG,
video), full post-process chains, model loading and session creation with
a small test model.

### 7.5 End-to-End Tests

Full binary execution with real models, real files, real hardware. Slow and
hardware-dependent. Run locally on demand, nightly in CI, mandatory before
release.

### 7.6 Performance Benchmarks

Frame processing time, memory peak, startup time, video throughput. Tracked
over time. Regressions > 20% block the PR.

### 7.7 Test Tags

| Tag | Scope | Schedule |
|-----|-------|----------|
| `[unit]` | Fast, no I/O, no GPU | Every commit |
| `[integration]` | May use disk, no GPU | Every push, CI |
| `[e2e]` | Real model + hardware | Nightly, release |
| `[regression]` | Bug reproductions | Same as parent level |
| `[benchmark]` | Performance tracking | Nightly |

Domain tags (`[color]`, `[frameio]`, `[inference]`, `[device]`) are combined
with level tags.

---

## 8. Git Workflow

### 8.1 Branch Strategy

`main` is the integration branch. Keep it releasable by merging only after
local quality gates pass and review is complete.

Branch prefixes: `feat/`, `fix/`, `chore/`, `test/`, `refactor/`.

### 8.2 Commit Messages

[Conventional Commits](https://www.conventionalcommits.org/). Prefixes:
`feat`, `fix`, `test`, `refactor`, `chore`, `docs`, `perf`.

Short imperative sentence. Body (optional) explains why, not what.

### 8.3 Developer Setup

Development setup instructions are in `CONTRIBUTING.md`. That is the single
source of truth for onboarding.

---

## 9. Documentation Standards

### 9.1 Core Principle

Documentation contains **definitions and decisions** — what the project is,
what it does, how it works, and why. It serves as a reference for anyone
building, using, or contributing to the project.

### 9.2 What Documentation MUST Contain

- **Business context:** Every document and every major section starts with
  why something exists and what problem it solves, before technical details.
- **Definitions:** Concrete terms, types, interfaces, behaviors, constraints.
- **Specifications of committed work:** Features and designs that are decided.
  Documented as facts, not wishes.
- **Current state:** What is built, how it works, how to use it.

### 9.3 What Documentation MUST NOT Contain

- **Speculation or unfounded plans.** Use GitHub Issues for proposals.
- **Historical logs.** Git history and PRs serve that purpose.
- **Dates, version stamps, or status markers.** The current state of the
  document IS the document.
- **Source code.** Pseudocode is acceptable only when necessary to explain an
  algorithm that cannot be expressed clearly in prose. CLI usage examples in
  README are acceptable because they document the user interface.
- **Emoji.** Plain, professional language.
- **Filler text.** No "this section will be expanded later", no placeholders.
  If a section is not ready, it does not exist.
- **Duplicated content.** Each piece of information lives in one place.

### 9.4 Document Purposes

| Document | Scope | Audience |
|----------|-------|----------|
| `README.md` | What is this, how to install, how to use | Users, potential contributors |
| `CONTRIBUTING.md` | Dev environment setup, how to submit changes | Contributors |
| `docs/SPEC.md` | What to build and why | Architects, developers |
| `docs/GUIDELINES.md` | How to build it (this document) | Developers |
| `docs/ARCHITECTURE.md` | Where code lives | Developers |
| `docs/FRONTEND.md` | UI/UX specifications and frontend tech stack | Frontend developers |
| `docs/PLAN.md` | Explicit exception for active implementation context and checklists | Maintainers |
| `docs/RELEASE_GUIDELINES.md`| Standard operating procedure for building and releasing | Release maintainers |
| `CLAUDE.md` | Machine-readable rule summary | AI tools |

### 9.5 Living Documentation in Code

The code is the primary source of truth. Documentation in code captures
**intent and context** that the code alone cannot convey.

**Public API** (`include/corridorkey/`): Doxygen-style doc comments on every
public function and class. States purpose, parameters, return value,
preconditions, error conditions. This is the API reference — it lives in
code because the code IS the contract.

**Internal code — document:**
- Why a decision was made (business reason, performance trade-off, workaround)
- Constraints not obvious from code (color space requirements, alignment)
- References to external specifications (IEC 61966-2-1, OpenEXR conventions)

**Internal code — do NOT document:**
- What the code does (refactor to make it self-evident)
- How the code works line by line (the code is readable)
- Commented-out code (delete it, use version control)
- TODO/FIXME/HACK (open a GitHub Issue)
- Decorative comments (banners, dividers, ASCII art)

**Tests as documentation:** A well-named test documents a requirement more
reliably than any comment, because it is verified on every build.

### 9.6 Writing Style

- Plain English, professional tone
- Active voice, direct statements
- Concrete and specific
- Technical terms defined on first use
- Consistent terminology throughout

---

## 10. Security

- No network access in the core library. Model download is a separate CLI
  command, not implicit.
- Input validation at all system boundaries: file headers, resolution limits,
  parameter ranges.
- No shell command execution from the library.
- FFmpeg input is treated as untrusted. Use FFmpeg's built-in limits and
  timeouts.
- Dependency audit via vcpkg vulnerability scanning in CI.

---

## 11. OpenFX Robustness

### 11.1 Zero Exception Leakage
OpenFX is a pure C API. If a C++ exception escapes any of our exported plugin functions, the host application (like DaVinci Resolve) will instantaneously crash to desktop. 
- All `extern "C"` endpoints and the OFX `plugin_main_entry` must be safely wrapped in a global `try/catch` block.
- Internal exceptions must be intercepted, logged, and gracefully translated into a safe C status code (e.g., `kOfxStatFailed`).

### 11.2 Zero-Allocation Post Processing
Because DaVinci Resolve schedules aggressive parallel render threads (one per frame/core), memory fragmentation and out-of-memory errors are the primary cause of OFX instability.
- Do not instantiate large local containers (`std::vector<T>`) inside per-pixel or per-frame functions (e.g., inside `src/post_process/`).
- Utilize persistent `ScratchState` structs that use `std::vector::reserve()` and `std::vector::resize()` to maintain capacities across continuous frames, avoiding calls to the OS memory allocator. Memory must belong to the OFX instance lifecycle or the Job Orchestrator, rather than being discarded at the end of every frame.
