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
  ↓ depends on
Public API (include/corridorkey/)
  ↓ depends on
Core Logic (src/ — inference, post-process, frame I/O)
  ↓ depends on
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

| Principle | How we apply it |
|-----------|----------------|
| **S** — Single Responsibility | Each class/file has one reason to change. `ExrReader` reads EXR. `Despill` does despill. No god objects. |
| **O** — Open/Closed | New execution providers or image formats are added without modifying existing code — via interfaces and registration. |
| **L** — Liskov Substitution | Any `ImageReader` implementation (EXR, PNG, Video) is interchangeable through the base interface without surprises. |
| **I** — Interface Segregation | Clients depend only on the interfaces they use. The CLI doesn't see internal inference details. |
| **D** — Dependency Inversion | `InferenceEngine` depends on an abstract `ISession`, not directly on ONNX Runtime types. External libs are behind wrappers. |

### 1.3 Object Calisthenics (adapted for C++)

These are **guiding constraints**, not absolute laws. They push toward cleaner
code. When a rule makes the code worse, document why you're breaking it.

| Rule | C++ Adaptation | Enforced by |
|------|---------------|-------------|
| **1. One level of indentation per function** | Max 2 levels of nesting. Extract helper functions instead. | clang-tidy: readability-function-cognitive-complexity (threshold: 15) |
| **2. Don't use else** | Prefer early returns, guard clauses, and std::optional/std::expected. Else is permitted for simple if/else pairs. | Code review |
| **3. Wrap primitives** | Domain types: `Resolution`, `DespillStrength`, `AlphaValue` instead of raw int/float where semantics matter. Don't over-wrap trivially obvious parameters. | Code review |
| **4. First-class collections** | Wrap collections that carry behavior: `FrameSequence` instead of `std::vector<Frame>` with external logic. Plain vectors are fine for simple data. | Code review |
| **5. One dot per line** | Avoid long chains. Intermediate variables with descriptive names. Method chaining in builders is acceptable. | Code review |
| **6. Don't abbreviate** | Full names: `alpha_hint`, not `ah`. `frame_count`, not `fc`. Standard abbreviations OK: `fps`, `rgb`, `exr`, `io`. | clang-tidy: readability-identifier-naming |
| **7. Keep classes small** | Target: < 200 lines per .cpp file, < 100 lines per .hpp. Split when it grows. | CI check (script) |
| **8. Max two instance variables** | Relaxed to: prefer small structs. When a class has > 5 members, consider if it should be split. | Code review |
| **9. No getters/setters** | Prefer immutable value types (structs with public const members) or methods that express behavior. Trivial POD structs with public members are fine. | Code review |

### 1.4 Additional Code Standards

- **Const correctness:** Everything that can be const, is const. Parameters,
  local variables, member functions, return values.
- **RAII everywhere:** No raw `new`/`delete`. Use `std::unique_ptr`,
  `std::shared_ptr`, stack allocation. FFmpeg/OpenEXR C resources wrapped
  in RAII handles.
- **No raw owning pointers.** Non-owning observation via raw pointer or
  `std::span` is fine.
- **Error handling:** Use return types (`std::expected<T, Error>` or
  `std::optional<T>`) for expected failures. Exceptions only for truly
  exceptional conditions (out of memory, corrupted file).
- **No global mutable state.** Configuration passed explicitly through
  parameters or dependency injection.
- **Naming conventions:**
  - `snake_case` for functions, variables, namespaces, files
  - `PascalCase` for types (classes, structs, enums, concepts)
  - `UPPER_SNAKE_CASE` for compile-time constants and macros
  - `m_` prefix for private member variables
  - All names in English

---

## 2. API Design for Reusability

To ensure the library remains a first-class citizen capable of evolving into various frontends, adhere to these API design rules:

### 2.1 Interface Stability
- **PIMPL Pattern (Pointer to Implementation):** Use PIMPL for the main `Engine` class. This hides implementation details (like ONNX Runtime types) from the public headers, ensuring ABI stability and keeping headers clean.
- **Minimal Public Headers:** Only expose what is absolutely necessary. Keep `include/corridorkey/` lean.
- **Resource Management:** Use RAII for all library objects. The user should not have to manually call `init()` or `cleanup()` for global states.

### 2.2 Execution & Threading
- **Non-Blocking Support:** Provide asynchronous versions of heavy operations or ensure they can be easily wrapped in a thread by the consumer.
- **Progress Callbacks:** Long-running operations (like `process_sequence`) must accept a callback function to report progress and allow for cancellation.
- **Thread Safety:** The `Engine` class should be thread-safe for concurrent read operations. Document any shared state that requires synchronization.

### 2.3 Error Handling
- **No `std::exit` or `abort`:** The library must never terminate the process.
- **Explicit Results:** Use `std::expected<T, Error>` for all operations that can fail (I/O, model loading, inference).
- Error Context: Provide descriptive error codes and messages that can be displayed in any UI (CLI or GUI).

---

## 3. Build System & Dependency Management

To ensure absolute reproducibility and prevent "it works on my machine" issues, the following infrastructure rules apply:

### 3.1 Dependency Pinning (vcpkg)
- **Manifest Mode:** Always use `vcpkg.json`.
- **Baseline Versioning:** A `vcpkg-configuration.json` file MUST be present in the root. It must specify a `builtin-baseline` (a Git commit hash from the official vcpkg repository). This ensures every developer and CI runner uses the exact same versions of ONNX Runtime, OpenEXR, and FFmpeg.
- **Explicit Features:** When adding a dependency in `vcpkg.json`, explicitly list the required features (e.g., FFmpeg with `avcodec` and `swscale`) and provide a `$comment` explaining the usage.

### 3.2 Modern CMake (Target-Based)
- **No Global Commands:** Commands like `include_directories`, `link_directories`, or `add_definitions` are strictly prohibited.
- **Target-Specific Configuration:** All settings (include paths, compile definitions, compiler flags) must be attached to targets via `target_include_directories`, `target_compile_definitions`, and `target_compile_options`.
- **Visibility Control:** The library must be built with `CMAKE_CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN ON`. Only symbols explicitly marked with the `CORRIDORKEY_API` macro (generated via `GenerateExportHeader`) will be exported. This prevents internal library symbols (like ONNX Runtime) from leaking into the consumer's namespace.

### 3.3 CMake Presets
- **Source of Truth:** `CMakePresets.json` is the only supported way to configure the project. It must contain presets for `debug`, `release`, and `ci` environments.
- **Zero-Manual-Setup:** Presets must automatically locate the vcpkg toolchain file. A developer should only need to run `cmake --preset debug` to get a working build.

### 3.4 Compiler Rigor & Sanitizers
- **Strict Warnings:** Use `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) or `/W4 /WX` (MSVC).
- **Address Sanitizer (ASAN):** Enabled by default in the `debug` preset to catch memory leaks and buffer overflows early.

---

## 4. Testing Strategy

### 2.1 Test Pyramid

```
          ┌───────────┐
          │  E2E (few) │   Full binary, real models, real files
          ├───────────┤
        ┌─┤Integration ├─┐  Multiple modules together
        │ │  (moderate) │ │
        │ ├─────────────┤ │
        │ │    Unit      │ │  Single function/class in isolation
        │ │   (many)     │ │
        └─┴─────────────┴─┘
```

We do NOT aim for 100% unit test coverage. We aim for **high-value coverage
across the full pyramid** — every layer contributes confidence that the system
works correctly.

### 2.2 Unit Tests

**Scope:** Single function or class, no external dependencies (no disk, no GPU,
no model files).

**What to test:**
- `color_utils`: sRGB↔linear conversion accuracy, edge cases (0.0, 1.0,
  negative, > 1.0), round-trip consistency
- `despill`: known input/output pairs, strength=0 passthrough, edge cases
- `despeckle`: known patterns, empty input, single-pixel
- `device_detection`: mock hardware info, tier classification logic
- `resolution_selection`: memory→resolution mapping, edge cases
- Value types: `Resolution`, `InferenceParams` construction and validation

**What NOT to unit test:**
- Thin wrappers around external libraries (OpenEXR, FFmpeg) — tested at
  integration level
- Trivial constructors, getters on POD types
- Private implementation details

**Framework:** Catch2 v3

### 2.3 Regression Tests

**Scope:** Prevent specific bugs from reoccurring.

**Process:**
1. Bug is found and reported
2. A failing test is written FIRST that reproduces the bug
3. The fix is implemented
4. Test passes and is committed with a descriptive name referencing the issue

**Naming convention:** `test_regression_<issue_number>_<short_description>`

**Regression tests are never deleted.** They are the project's scar tissue —
proof we don't repeat mistakes.

### 2.4 Integration Tests

**Scope:** Multiple modules working together, may use real files but NOT real
GPU inference (too slow, hardware-dependent).

**What to test:**
- FrameIO pipeline: read EXR → process in memory → write EXR → read back →
  compare (round-trip)
- FrameIO pipeline: read PNG → write PNG → compare
- Video pipeline: decode MP4 frame → re-encode → decode → compare
- PostProcess chain: full despill+despeckle+composite pipeline on known input
- InferenceEngine: model loading, session creation, input/output shape
  validation (with a tiny test model, not the real 300MB model)

**Test fixtures:** Small reference files stored in `tests/fixtures/`. Kept
under 1MB total in the repository. Larger test assets downloaded on demand
(CI only).

### 2.5 End-to-End Tests

**Scope:** Full binary execution, real model, real files, real hardware.

**What to test:**
- `corridorkey process frame.png --alpha-hint hint.png -o out.png` produces
  valid output matching reference within tolerance
- `corridorkey process input.mp4 --alpha-hint hint.mp4 -o out.mp4` produces
  playable video with correct frame count
- `corridorkey info` prints device info without crashing
- `corridorkey download --variant int8` downloads model successfully
- Output directory structure matches spec (Matte/, FG/, Processed/, Comp/)

**Execution:** E2E tests are slow and hardware-dependent. They run:
- Locally: on demand (`ctest --label-regex e2e`)
- CI: nightly, not on every push
- Release: mandatory before any version tag

### 2.6 Performance Tests (Benchmarks)

Not part of the test pyramid but important for regression:

- Frame processing time (per resolution, per EP, per model variant)
- Memory usage peak
- Startup time (model load + warm-up)
- Video throughput (fps)

Benchmarks are tracked over time. Significant regressions (> 20% slowdown)
block the PR.

### 2.7 Test Tags (Catch2)

```
[unit]          — Unit tests, fast, no I/O, no GPU
[integration]   — Integration tests, may use disk, no GPU
[e2e]           — End-to-end, needs real model + hardware
[regression]    — Bug regression tests
[benchmark]     — Performance benchmarks
[color]         — Color math tests
[frameio]       — Frame I/O tests
[inference]     — Inference engine tests
[device]        — Device detection tests
```

Run subsets:
```bash
ctest --label-regex unit          # fast, every commit
ctest --label-regex integration   # moderate, every push
ctest --label-regex e2e           # slow, nightly/release
```

---

## 3. Static Analysis & Formatting

### 3.1 Tools

| Tool | Purpose | Config file |
|------|---------|-------------|
| **clang-format 18+** | Code formatting (whitespace, braces, includes) | `.clang-format` |
| **clang-tidy 18+** | Static analysis, bug detection, style enforcement | `.clang-tidy` |
| **cppcheck** | Additional static analysis (undefined behavior, leaks) | `cppcheck.cfg` |
| **include-what-you-use** | Ensure headers are minimal and correct | CMake integration |

### 3.2 clang-format Configuration

Based on LLVM style with project-specific overrides:

```yaml
# .clang-format
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^<corridorkey/'
    Priority: 1
  - Regex: '^<(onnxruntime|OpenEXR|Imath)/'
    Priority: 2
  - Regex: '^<'
    Priority: 3
  - Regex: '.*'
    Priority: 4
SortIncludes: CaseSensitive
PointerAlignment: Left
ReferenceAlignment: Left
SpaceAfterCStyleCast: false
```

### 3.3 clang-tidy Configuration

Strict but practical. Every check is intentional.

```yaml
# .clang-tidy
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  cert-*,
  -cert-err58-cpp,
  cppcoreguidelines-*,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-bounds-array-to-pointer-decay,
  -cppcoreguidelines-owning-memory,
  misc-*,
  -misc-non-private-member-variables-in-classes,
  modernize-*,
  -modernize-use-trailing-return-type,
  performance-*,
  readability-*,
  -readability-magic-numbers,
  -readability-identifier-length,
  concurrency-*,

WarningsAsErrors: >
  bugprone-*,
  cert-*,
  cppcoreguidelines-slicing,
  cppcoreguidelines-no-malloc,
  performance-unnecessary-copy-initialization,
  readability-function-cognitive-complexity,

CheckOptions:
  - key: readability-function-cognitive-complexity.Threshold
    value: 15
  - key: readability-identifier-naming.NamespaceCase
    value: lower_case
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: lower_case
  - key: readability-identifier-naming.VariableCase
    value: lower_case
  - key: readability-identifier-naming.PrivateMemberSuffix
    value: ''
  - key: readability-identifier-naming.PrivateMemberPrefix
    value: 'm_'
  - key: readability-identifier-naming.ConstantCase
    value: UPPER_CASE
  - key: readability-identifier-naming.EnumConstantCase
    value: CamelCase
  - key: cppcoreguidelines-special-member-functions.AllowSoleDefaultDtor
    value: true
  - key: misc-non-private-member-variables-in-classes.IgnoreClassesWithAllMemberVariablesBeingPublic
    value: true

HeaderFilterRegex: 'include/corridorkey/.*\.hpp$|src/.*\.hpp$'
```

### 3.4 cppcheck Configuration

```
# cppcheck.cfg (command-line flags)
--enable=warning,style,performance,portability
--suppress=missingIncludeSystem
--suppress=unmatchedSuppression
--error-exitcode=1
--inline-suppr
--std=c++20
```

---

## 4. Pre-flight Checks (Git Hooks)

### 4.1 Overview

Nothing reaches the repository without passing quality gates. Two levels:

```
Developer commits code
        ↓
  ┌─────────────────────┐
  │  pre-commit hook     │  Fast checks (< 30s)
  │  (runs on staged     │  - formatting
  │   files only)        │  - lint (clang-tidy on changed files)
  │                      │  - build check
  └─────────────────────┘
        ↓ pass
  git commit created
        ↓
  Developer pushes
        ↓
  ┌─────────────────────┐
  │  pre-push hook       │  Thorough checks (< 5min)
  │  (runs on full       │  - full build (debug + release)
  │   codebase)          │  - all unit tests
  │                      │  - all integration tests
  │                      │  - cppcheck full scan
  │                      │  - file size limits
  └─────────────────────┘
        ↓ pass
  push to remote
        ↓
  ┌─────────────────────┐
  │  CI pipeline         │  Full validation (< 15min)
  │  (GitHub Actions)    │  - build on macOS, Linux, Windows
  │                      │  - all tests (unit + integration)
  │                      │  - clang-tidy full codebase
  │                      │  - cppcheck full codebase
  │                      │  - binary size check
  │                      │  - IWYU check
  └─────────────────────┘
```

### 4.2 pre-commit Hook

Managed via [pre-commit](https://pre-commit.com/) framework:

```yaml
# .pre-commit-config.yaml
repos:
  # Format check (fast — only checks, doesn't modify)
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v18.1.0
    hooks:
      - id: clang-format
        args: [--dry-run, --Werror]
        types_or: [c, c++]

  # Lint (on changed files only)
  - repo: https://github.com/cpp-linter/cpp-linter-hooks
    rev: v0.7.0
    hooks:
      - id: clang-tidy
        args: [--config-file=.clang-tidy, -p=build]
        types_or: [c, c++]

  # Basic file hygiene
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v4.6.0
    hooks:
      - id: trailing-whitespace
      - id: end-of-file-fixer
      - id: check-yaml
      - id: check-json
      - id: check-added-large-files
        args: [--maxkb=500]
      - id: no-commit-to-branch
        args: [--branch, main]
```

### 4.3 pre-push Hook

A shell script that runs the full local validation:

```bash
#!/usr/bin/env bash
# .githooks/pre-push
set -euo pipefail

echo "=== Pre-push checks ==="

echo "[1/4] Building (Debug)..."
cmake --build build/debug --parallel

echo "[2/4] Running unit tests..."
ctest --test-dir build/debug --label-regex unit --output-on-failure

echo "[3/4] Running integration tests..."
ctest --test-dir build/debug --label-regex integration --output-on-failure

echo "[4/4] Running cppcheck..."
cppcheck --project=build/debug/compile_commands.json \
    --enable=warning,style,performance,portability \
    --error-exitcode=1 --suppress=missingIncludeSystem

echo "=== All pre-push checks passed ==="
```

### 4.4 CI Pipeline (GitHub Actions)

```yaml
# Summary of CI jobs (full config in .github/workflows/ci.yml)

on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-24.04, macos-14, windows-2022]
    steps:
      - checkout
      - setup vcpkg
      - cmake configure + build (Debug)
      - unit tests
      - integration tests
      - clang-tidy (full codebase)
      - cppcheck (full codebase)

  quality-gates:
    steps:
      - clang-format check (full codebase)
      - file size limits (no .cpp > 200 lines, no .hpp > 100 lines — warning, not blocking)
      - include-what-you-use
      - binary size check (warn if > 20MB)

  nightly:  # separate workflow, runs daily
    steps:
      - build (Release)
      - e2e tests (with real model)
      - benchmarks (track over time)
```

---

## 5. Git Workflow

### 5.1 Branch Strategy

```
main              ← always stable, all checks pass
  └── feat/xxx    ← feature branches, PR to main
  └── fix/xxx     ← bug fix branches, PR to main
  └── chore/xxx   ← maintenance (deps, CI, docs)
```

- **main** is protected: requires PR, passing CI, and review
- Direct commits to main are blocked (enforced by pre-commit hook +
  GitHub branch protection)
- Squash merge preferred for clean history

### 5.2 Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add EXR 16-bit read support
fix: prevent NaN in despill with zero alpha
test: add regression test for #42
refactor: extract RAII wrapper for FFmpeg context
chore: update ONNX Runtime to 1.25
docs: add hardware tier table to README
perf: parallelize frame decode pipeline
```

### 5.3 Setup for New Developers

```bash
# Clone and setup
git clone <repo>
cd CorridorKey-Runtime

# Install pre-commit hooks
pip install pre-commit   # or: brew install pre-commit
pre-commit install
pre-commit install --hook-type pre-push

# Configure git to use project hooks
git config core.hooksPath .githooks

# Build
cmake --preset debug
cmake --build build/debug --parallel

# Test
ctest --test-dir build/debug --label-regex unit
```

---

## 6. Documentation Standards

### 6.1 Core Principle

Documentation contains **definitions and decisions** — what the project is,
what it does, how it works, and why. It serves as a reference for anyone
building, using, or contributing to the project.

### 6.2 What Documentation MUST Contain

- **Business context:** Every document and every major section starts with
  *why* something exists and what problem it solves, before diving into
  technical details.
- **Definitions:** Concrete terms, types, interfaces, behaviors, constraints.
  If something is decided, it is documented as a fact.
- **Specifications of planned work:** Features and designs that are committed
  and will be implemented. Documented as decisions, not wishes.
- **Current state:** What is built, how it works, how to use it.

### 6.3 What Documentation MUST NOT Contain

- **Speculation or unfounded plans.** If it is not decided, it does not go in
  the docs. Use GitHub Issues for proposals and discussions.
- **Historical logs.** Do not record when something was done, what changed, or
  what was removed. Git history and GitHub PRs serve that purpose.
- **Dates, version stamps, or status markers** (no "DRAFT v0.1", no
  "Updated 2026-03-08"). The current state of the document IS the document.
  Git tracks the rest.
- **Source code in documentation.** Documentation describes behavior, interfaces,
  and decisions. It does not contain implementation code. Pseudocode is
  acceptable only when necessary to explain an algorithm or flow that cannot be
  expressed clearly in prose. The place for code is in the codebase; the place
  for documentation is in the docs. CLI usage examples in README are acceptable
  because they document the user interface.
- **Emoji.** Not in documentation, not in code comments, not in commit
  messages. Plain, professional language.
- **Filler text.** No "this section will be expanded later", no placeholders,
  no TODO markers in documentation. If a section is not ready, it does not
  exist yet.
- **Duplicated content.** Each piece of information lives in one place.
  Other documents reference it, not copy it.

### 6.4 Document Purposes

Each document has a clear, non-overlapping scope:

| Document | Scope | Audience |
|----------|-------|----------|
| `README.md` | What is this, how to install, how to use | Users and potential contributors |
| `CONTRIBUTING.md` | How to set up dev environment, how to submit changes | Contributors |
| `docs/SPEC.md` | What to build and why: architecture, components, interfaces, hardware tiers, model pipeline | Architects and developers |
| `docs/GUIDELINES.md` | How to build it: code standards, testing, linting, git workflow (this document) | Developers |
| `docs/ARCHITECTURE.md` | Where code lives: project structure, directory rules, dependency layers | Developers |
| `CLAUDE.md` | Machine-readable summary of rules for AI-assisted development | AI tools |

If information does not fit any of these, it either belongs in code comments
(for implementation-level context) or in a GitHub Issue (for discussion).

### 6.5 Code Documentation (Living Documentation)

The code is the primary source of truth for how things work. Documentation in
code exists to capture **intent and context** that the code alone cannot convey.

**Public API** (`include/corridorkey/`):
- Every public function and class has a Doxygen-style doc comment that states
  its purpose, parameters, return value, preconditions, and error conditions.
- This is the API reference. It lives in the code because the code IS the
  contract. When the interface changes, the documentation changes with it.

**Internal code — what to document:**
- **Why** a decision was made (business reason, performance trade-off,
  workaround for a known issue in an external library).
- **Constraints** that are not obvious from the code (color values must be in
  linear space, not sRGB; buffer must be aligned to 16 bytes).
- **References** to external specifications (sRGB transfer function per IEC
  61966-2-1, OpenEXR channel naming conventions).

**Internal code — what NOT to document:**
- **What** the code does. If a comment restates the code in English, delete
  the comment and rename the function or variable to be self-explanatory.
- **How** the code works line by line. The code is readable; if it is not,
  refactor it.
- **Commented-out code.** Delete it. Version control is the archive.
- **TODO/FIXME/HACK markers.** Open a GitHub Issue instead. The issue tracker
  is the backlog, not the source code.
- **Decorative comments** (section banners, ASCII art, dividers). The file
  structure and naming provide organization.

**The test as documentation:**
- Well-named tests are the most reliable documentation of behavior. A test
  named `despill_preserves_luminance_for_neutral_colors` documents a
  requirement more reliably than any comment, because it is verified on every
  build.

### 6.6 Writing Style

- Plain English, professional tone
- Active voice, direct statements ("The engine loads the model" not "The model
  is loaded by the engine")
- Concrete and specific ("processes frames at 512x512 resolution" not
  "processes frames at a suitable resolution")
- Technical terms are defined on first use
- Consistent terminology throughout (pick one term for a concept and use it
  everywhere)

---

## 7. Dependency Management

### 7.1 Rules

- Every external dependency must be justified in a comment in `vcpkg.json`
- Prefer header-only libraries for small utilities (CLI11, stb)
- Vendor header-only libs in `vendor/` with version noted
- Lock vcpkg baseline in `vcpkg.json` for reproducible builds
- Update dependencies quarterly, not continuously
- Security vulnerabilities are patched immediately regardless of schedule

### 7.2 vcpkg.json Structure

```json
{
    "$comment": "Dependencies for CorridorKey Optimized",
    "name": "corridorkey-runtime",
    "version-string": "0.1.0",
    "dependencies": [
        { "name": "onnxruntime", "$comment": "ML inference runtime" },
        { "name": "openexr", "$comment": "VFX-standard image format" },
        { "name": "libpng", "$comment": "16-bit PNG support" },
        { "name": "ffmpeg", "$comment": "Video decode/encode",
          "features": ["avcodec", "avformat", "swscale"] },
        { "name": "catch2", "$comment": "Testing framework" }
    ],
    "builtin-baseline": "<locked-commit-hash>"
}
```

---

## 8. Security Considerations

- No network access in the core library. Model download is a separate CLI
  command, not implicit.
- Input validation at all system boundaries: file headers, resolution limits,
  parameter ranges.
- No shell command execution from the library.
- FFmpeg input is treated as untrusted (fuzzed file formats are a known
  attack vector). Use FFmpeg's built-in limits and timeouts.
- Dependency audit via `vcpkg` vulnerability scanning in CI.
