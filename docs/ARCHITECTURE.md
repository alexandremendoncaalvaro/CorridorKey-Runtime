# Project Architecture

> This document defines the project structure, the purpose of every directory,
> and the rules that govern where code lives. It is the **single source of
> truth** for structural decisions. Any deviation must be discussed and
> approved in a PR before it happens.
>
> **See also:**
> [SPEC.md](SPEC.md) — technical specification |
> [GUIDELINES.md](GUIDELINES.md) — code standards, testing, linting

---

## 1. Structural Principles

This project follows the **Pitchfork layout** (separated variant) adapted for
our needs, combined with **Clean Architecture** layering.

Three non-negotiable rules:

1. **Public API is in `include/`, implementation is in `src/`.** No exceptions.
2. **Dependencies flow downward only.** CLI depends on the library. The library
   never depends on the CLI. Lower layers never include headers from upper
   layers.
3. **One responsibility per directory.** Each `src/` subdirectory owns a single
   domain. Cross-domain logic goes through the public API layer.

---

## 2. Directory Map

Every directory in the project is listed here with its purpose, rules, and
what MUST NOT go in it.

### Root (`/`)

Configuration and project-level documentation only.

| File | Purpose | Rule |
|------|---------|------|
| `CMakeLists.txt` | Root build definition | Defines the library and links subdirectories. Does NOT contain source file lists — those live in each subdirectory's own CMakeLists.txt. |
| `CMakePresets.json` | Build presets (debug, release) | Cross-platform. No hardcoded paths. |
| `vcpkg.json` | Dependency manifest | Every dependency has a `$comment` explaining why it exists. |
| `.clang-format` | Formatting rules | Do not override per-directory. One style for the whole project. |
| `.clang-tidy` | Static analysis | Do not weaken checks without a documented reason. |
| `.pre-commit-config.yaml` | Git hook definitions | Covers pre-commit and pre-push. |
| `.gitignore` | VCS exclusions | Keep minimal. No IDE-specific entries except universal ones. |
| `LICENSE` | CC BY-NC-SA 4.0 | Do not modify. |
| `README.md` | User-facing documentation | Installation, usage, hardware support. Not architecture. |
| `CONTRIBUTING.md` | Developer onboarding | Setup, standards summary, PR process. |
| `CLAUDE.md` | AI-assisted development rules | Read automatically by Claude Code. Keep in sync with this document. |

**MUST NOT be in root:** source code, test files, build artifacts, model files,
scripts, documentation beyond README/CONTRIBUTING.

### `.github/`

GitHub-specific automation and templates.

```
.github/
├── workflows/          CI/CD pipeline definitions (YAML)
├── ISSUE_TEMPLATE/
│   ├── bug_report.md
│   └── feature_request.md
└── PULL_REQUEST_TEMPLATE.md
```

**Rule:** CI workflows must build and test on all three platforms (macOS, Linux,
Windows). No platform-specific workarounds that skip tests.

### `cmake/`

CMake helper modules: Find*.cmake files, custom functions, toolchain files.

**Rule:** No source file lists here. This directory contains build system
utilities, not project configuration.

### `docs/`

Technical documentation for developers and architects.

```
docs/
├── ARCHITECTURE.md     This document — structure and rules
├── SPEC.md             Technical specification — what to build and why
└── GUIDELINES.md       Engineering standards — how to build it
```

**Rule:** Keep documents focused. SPEC is about *what*, GUIDELINES is about
*how*, ARCHITECTURE is about *where*. Do not duplicate content across them.

**MUST NOT be in docs:** user-facing documentation (that's README.md), API
reference (that's Doxygen in code comments).

### `include/corridorkey/`

**Public API headers only.** These are the headers that external consumers of
the library would include.

```
include/corridorkey/
├── engine.hpp          Engine class — the main entry point
├── types.hpp           Value types: Image, DeviceInfo, InferenceParams, etc.
├── frame_io.hpp        FrameIO interface (read/write images and video)
└── version.hpp         Version macros (major, minor, patch)
```

**Rules:**
- Every header here is part of the public contract. Changes are breaking.
- Headers use `#pragma once` as include guard.
- No implementation in headers (except templates and inline functions).
- No internal/private types. If it's not meant for external use, it goes in
  `src/` as a private header.
- Namespace: everything under `corridorkey::`.
- Include path: `#include <corridorkey/engine.hpp>`.

**MUST NOT be in include/:** implementation files (.cpp), private headers,
third-party headers, test files.

### `src/`

All implementation code. Organized by domain, not by file type.

```
src/
├── cli/                CLI application (thin consumer of the library)
│   └── main.cpp
├── core/               Inference engine, device detection, session management
│   ├── engine.cpp
│   ├── inference_session.cpp
│   ├── inference_session.hpp   (private header)
│   └── device_detection.cpp
├── frame_io/           Image and video read/write
│   ├── exr_io.cpp
│   ├── png_io.cpp
│   └── video_io.cpp
└── post_process/       Color math, despill, despeckle
    ├── color_utils.cpp
    ├── despill.cpp
    └── despeckle.cpp
```

**Rules:**

- **`src/cli/`** — The CLI entry point. This directory contains ONLY the
  `main()` function and argument parsing. No business logic. No image
  processing. It constructs an `Engine`, calls methods, and prints results.
  If you're writing an `if` that checks pixel values in `cli/`, you're in
  the wrong place.

- **`src/core/`** — The inference engine and hardware management. Wraps ONNX
  Runtime behind our own abstractions. External library types (OrtSession,
  OrtValue, etc.) must NOT leak into the public API. They stay behind private
  headers in this directory.

- **`src/frame_io/`** — All file format handling. OpenEXR, libpng, stb_image,
  and FFmpeg are confined here. No other directory should include headers from
  these libraries directly.

- **`src/post_process/`** — Pure computation on pixel data. Color space
  conversion, despill, despeckle, compositing. These functions take arrays of
  floats and return arrays of floats. No file I/O, no GPU calls, no external
  library dependencies (except standard math). This makes them trivially
  testable.

**Private headers:** Files like `inference_session.hpp` that are needed across
.cpp files within the same `src/` subdirectory but are NOT part of the public
API live next to their .cpp files in `src/`. They are never installed and never
included from outside their subdirectory.

**MUST NOT be in src/:** test files, scripts, documentation, model files.

**File size guidelines:**
- Target: < 200 lines per .cpp, < 100 lines per .hpp
- If a file grows beyond this, consider splitting by responsibility
- This is a guideline, not a hard rule — clarity beats arbitrary limits

### `tests/`

All test code, organized by test level (pyramid).

```
tests/
├── unit/               Fast, isolated, no I/O, no GPU, no model
│   ├── CMakeLists.txt
│   ├── test_color_utils.cpp
│   ├── test_despill.cpp
│   ├── test_despeckle.cpp
│   └── test_device_detection.cpp
├── integration/        Multi-module, may use real files, no GPU
│   ├── CMakeLists.txt
│   ├── test_exr_roundtrip.cpp
│   ├── test_png_roundtrip.cpp
│   └── test_post_process_chain.cpp
├── e2e/                Full binary, real model, real hardware
│   ├── CMakeLists.txt
│   └── test_full_pipeline.cpp
└── fixtures/           Reference files used by integration and e2e tests
    ├── reference_frame.png
    ├── reference_alpha.png
    └── reference_output.exr
```

**Rules:**
- **Unit tests** (`tests/unit/`) depend ONLY on the code under test and the
  test framework (Catch2). No file system, no network, no GPU. They must
  complete in under 1 second each.
- **Integration tests** (`tests/integration/`) may read/write files from
  `tests/fixtures/`. They test module boundaries (e.g., read EXR → process →
  write EXR → compare).
- **E2E tests** (`tests/e2e/`) invoke the built binary or the full Engine API
  with real ONNX models. They are slow and hardware-dependent.
- **Fixtures** (`tests/fixtures/`) contains small reference files (< 1MB total
  in the repo). Larger test assets (models, video clips) are downloaded on
  demand by CI, never committed.
- Each test level has its own `CMakeLists.txt` and CTest labels (`unit`,
  `integration`, `e2e`).
- Test file naming: `test_<module_or_feature>.cpp`.
- Regression tests live in the appropriate level (usually `unit/`) and use the
  tag `[regression]` plus a reference to the issue number.

**MUST NOT be in tests/:** production code, scripts, documentation.

### `models/`

ONNX model files. **Entirely gitignored.** Downloaded on demand.

```
models/
├── .gitkeep
├── corridorkey_fp32.onnx    (downloaded)
├── corridorkey_fp16.onnx    (downloaded)
└── corridorkey_int8.onnx    (downloaded)
```

**Rule:** No model files in the git repository. They are large (75-300MB) and
versioned independently. The CLI `download` command fetches them from
HuggingFace.

### `scripts/`

Python scripts for model export and optimization. These run on the developer's
machine (typically Machine B with RTX 3080), not as part of the C++ build.

```
scripts/
├── export_onnx.py          PyTorch → ONNX export
├── optimize_model.py       onnxsim + transformer optimizer
├── quantize_model.py       Static INT8 quantization
└── validate_model.py       Compare ONNX vs PyTorch outputs
```

**Rule:** Scripts are utilities, not part of the C++ build. They have their own
Python dependencies (PyTorch, onnxruntime, etc.) and are documented in a
scripts/README.md.

**MUST NOT be in scripts/:** C++ source code, build scripts (those go in
`cmake/`), CI scripts (those go in `.github/workflows/`).

### `vendor/`

Vendored header-only third-party libraries.

```
vendor/
├── stb_image.h              Image reading (PNG, etc.)
├── stb_image_write.h        Image writing
└── CLI11.hpp                CLI argument parsing
```

**Rules:**
- Only header-only libraries. Compiled libraries go through vcpkg.
- Each vendored file has a comment at the top with the version and upstream URL.
- Updated manually and deliberately, not automatically.
- Vendored to avoid build-time downloads for trivial dependencies.

**MUST NOT be in vendor/:** our own code, compiled libraries, vcpkg-managed
dependencies.

---

## 3. Dependency Layers

```
┌─────────────────────────────────────────────────────┐
│ Layer 4: CLI                                         │
│ src/cli/                                             │
│ Depends on: Layer 3 (public API)                     │
│ Contains: main(), argument parsing, output formatting │
├─────────────────────────────────────────────────────┤
│ Layer 3: Public API                                   │
│ include/corridorkey/                                  │
│ Depends on: nothing (pure declarations)               │
│ Contains: Engine, Image, DeviceInfo, InferenceParams  │
├─────────────────────────────────────────────────────┤
│ Layer 2: Core Implementation                          │
│ src/core/ + src/frame_io/ + src/post_process/        │
│ Depends on: Layer 3 (implements its interfaces)       │
│           + Layer 1 (external libraries)              │
│ Contains: all business logic                          │
├─────────────────────────────────────────────────────┤
│ Layer 1: External Libraries                           │
│ ONNX Runtime, OpenEXR, FFmpeg, stb, libpng           │
│ Depends on: nothing in our project                    │
│ Wrapped behind interfaces — never leak into Layer 3   │
└─────────────────────────────────────────────────────┘
```

**Dependency direction is strictly downward.** A layer may only depend on
layers below it. Violations are caught by include-what-you-use and code review.

**External library isolation:** ONNX Runtime types, OpenEXR types, and FFmpeg
types never appear in `include/corridorkey/`. They are wrapped in `src/` behind
our own types. This means:
- Changing from OpenEXR to tinyexr affects only `src/frame_io/exr_io.cpp`
- Changing ONNX Runtime version affects only `src/core/`
- The public API is stable regardless of internal library changes

---

## 4. Frontend-Agnostic Core Principles

To ensure the library can be integrated into CLI, GUI, Web (WASM), or Server-side APIs without modification, the following rules apply:

1. **No Global State:** The library must not use global or static variables. All state must be contained within an `Engine` or `Session` instance.
2. **No Console I/O:** The library must never print to `stdout` or `stderr`. Logging must be handled via a callback-based logger or a standard logging interface that the consumer can redirect.
3. **No Direct Thread Management:** While the library can use internal threading (via ONNX Runtime or worker pools), it should provide a way for the consumer to control or observe execution. Long-running tasks must support cancellation and progress reporting via callbacks.
4. **Abstracted I/O:** The Core logic (`src/core/`) must not depend on the filesystem. It should work with memory buffers or abstract streams. Only `src/frame_io/` handles files, and it should be possible to bypass it for network-based or memory-based processing.
5. **Contextual Errors:** Return rich error types (`std::expected<T, Error>`) instead of throwing exceptions or exit the program. This allows consumers to handle errors gracefully in their own UI.

---

## 5. High-Performance Core Principles (2026 Standards)

The project is designed for "Elite Performance" by adhering to these low-level engineering standards:

1. **Zero-Copy Data Flow:** We use `std::span` (via the `Image` struct) for all image processing. Data is never copied when passed between modules. Ownership is strictly managed by `ImageBuffer`.
2. **SIMD Alignment:** All image buffers are allocated with 64-byte alignment (AVX-512 compatible). This allows the compiler to generate optimal vector instructions without needing unaligned load penalties.
3. **Data-Oriented Design (DOD):** We prioritize linear memory access. Operations on pixels are flattened to a single dimension where possible to maximize cache L1/L2 hits and enable prefetching.
4. **Memory Mapping (Planned):** For large model files and video sequences, we prefer memory-mapped I/O to offload memory management to the OS kernel.

---

## 6. Adding New Code — Decision Tree

```
Is it a public API type or function?
  YES → include/corridorkey/
  NO ↓

Is it inference, device detection, or session management?
  YES → src/core/
  NO ↓

Is it file format handling (read/write images or video)?
  YES → src/frame_io/
  NO ↓

Is it pixel computation (color math, filters, compositing)?
  YES → src/post_process/
  NO ↓

Is it CLI argument parsing or output formatting?
  YES → src/cli/
  NO ↓

Is it a build system helper or CMake module?
  YES → cmake/
  NO ↓

Is it a Python script for model export/optimization?
  YES → scripts/
  NO ↓

Is it a test?
  Pure logic test, no files   → tests/unit/
  Uses real files, no GPU     → tests/integration/
  Full pipeline, real model   → tests/e2e/
  Reference file for tests    → tests/fixtures/
  NO ↓

Is it project documentation?
  Architecture/structure  → docs/ARCHITECTURE.md (this file)
  What to build           → docs/SPEC.md
  How to build            → docs/GUIDELINES.md
  User-facing             → README.md
  Contributor-facing      → CONTRIBUTING.md
  NO ↓

Does it not fit anywhere above?
  → STOP. Discuss in an issue before creating new directories.
```

---

## 5. Rules for Structural Changes

1. **Do not create new top-level directories** without updating this document
   and getting approval in a PR.
2. **Do not create new `src/` subdirectories** without updating this document.
   The four subdirectories (`cli/`, `core/`, `frame_io/`, `post_process/`) are
   deliberate. If something doesn't fit, it probably belongs in an existing one.
3. **Do not move files between layers** without updating this document and the
   relevant CMakeLists.txt files.
4. **Do not add new vendored libraries** without justification. Prefer vcpkg
   for anything non-trivial.
5. **Do not commit generated files.** Build artifacts, model files, coverage
   reports, and IDE project files are gitignored.

---

## 6. File Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Public header | `include/corridorkey/<name>.hpp` | `engine.hpp` |
| Private header | `src/<module>/<name>.hpp` | `inference_session.hpp` |
| Implementation | `src/<module>/<name>.cpp` | `engine.cpp` |
| Unit test | `tests/unit/test_<name>.cpp` | `test_color_utils.cpp` |
| Integration test | `tests/integration/test_<name>.cpp` | `test_exr_roundtrip.cpp` |
| E2E test | `tests/e2e/test_<name>.cpp` | `test_full_pipeline.cpp` |
| CMake module | `cmake/<Name>.cmake` | `FindONNXRuntime.cmake` |
| Python script | `scripts/<name>.py` | `export_onnx.py` |

- All filenames are `snake_case`
- All C++ files use `.hpp` (headers) and `.cpp` (implementation)
- No `.h` files (we are a C++ project, not C)
