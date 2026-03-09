# Contributing to CorridorKey Runtime

Thank you for your interest in contributing. This document explains how to set
up your development environment, the standards we follow, and how to submit
changes.

## Development Setup

### Prerequisites

- C++20 compiler (GCC 12+, Clang 16+, or Apple Clang 15+)
- [CMake 3.28+](https://cmake.org/download/)
- [vcpkg](https://github.com/microsoft/vcpkg)
- [pre-commit](https://pre-commit.com/) (`pip install pre-commit` or
  `brew install pre-commit`)
- clang-format 18+ and clang-tidy 18+ (usually bundled with LLVM)

### First-Time Setup

```bash
# Clone
git clone https://github.com/<org>/CorridorKey-Runtime.git
cd CorridorKey-Runtime

# Install git hooks (mandatory)
pre-commit install
pre-commit install --hook-type pre-push

# Build (debug mode for development)
cmake --preset debug
cmake --build build/debug --parallel

# Run tests
ctest --test-dir build/debug --label-regex unit --output-on-failure
```

### Build Presets

| Preset | Use case |
|--------|----------|
| `debug` | Development — assertions, debug symbols, sanitizers |
| `release` | Distribution — optimized, no debug info |

## Code Standards

We follow the guidelines defined in [docs/GUIDELINES.md](docs/GUIDELINES.md).
Here is the summary:

### Architecture

- **Clean Architecture:** strict dependency direction (CLI -> API -> Core ->
  External). No upward or circular dependencies.
- **SOLID principles:** single responsibility, open/closed, Liskov substitution,
  interface segregation, dependency inversion.
- **Object Calisthenics (adapted):** max 2 nesting levels, prefer early returns,
  meaningful names (no abbreviations), small classes/files.

### Style

- **Formatting:** Enforced by clang-format. Do not argue about style — run the
  formatter.
- **Naming:** `snake_case` for functions/variables/files, `PascalCase` for
  types, `UPPER_SNAKE_CASE` for constants, `m_` prefix for private members.
- **Language:** All code, comments, and documentation in English.

### Quality

- Const correctness everywhere
- RAII for all resource management (no raw `new`/`delete`)
- Error handling via return types (`std::expected`, `std::optional`), not
  exceptions for expected failures
- No global mutable state

## Testing

We use a test pyramid. The goal is **high-value coverage across all layers**,
not 100% unit test coverage.

| Level | Tag | What to test |
|-------|-----|-------------|
| Unit | `[unit]` | Pure logic: color math, parameter validation, tier selection |
| Regression | `[regression]` | Specific bug reproductions — never deleted |
| Integration | `[integration]` | Multi-module: file I/O round-trips, post-process chains |
| E2E | `[e2e]` | Full binary with real model and files |

```bash
# Run unit tests (fast, every commit)
ctest --test-dir build/debug --label-regex unit

# Run integration tests
ctest --test-dir build/debug --label-regex integration

# Run everything locally
ctest --test-dir build/debug --output-on-failure
```

When fixing a bug:
1. Write a failing test that reproduces the bug
2. Fix the bug
3. Commit both the test and the fix

## Pre-flight Checks

Every commit and push must pass quality gates. These run automatically via git
hooks:

**On commit (< 30s):**
- clang-format (staged files)
- clang-tidy (staged files)
- File hygiene (trailing whitespace, large files, no commits to main)

**On push (< 5min):**
- Full debug build
- All unit and integration tests
- cppcheck full scan

If a hook fails, fix the issue and try again. Do **not** bypass hooks with
`--no-verify`.

## Submitting Changes

### Branch Naming

```
feat/short-description    # New feature
fix/short-description     # Bug fix
chore/short-description   # Maintenance (deps, CI, docs)
test/short-description    # Test additions/improvements
refactor/short-description # Code restructuring
```

### Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add EXR 16-bit read support
fix: prevent NaN in despill with zero alpha
test: add regression test for issue #42
refactor: extract RAII wrapper for FFmpeg context
chore: update ONNX Runtime to 1.25
docs: add hardware tier table to README
perf: parallelize frame decode pipeline
```

### Pull Request Process

1. Create a branch from `main`
2. Make your changes, ensuring all hooks pass
3. Push your branch and open a PR
4. Fill in the PR template
5. Wait for CI to pass and a review
6. Squash merge to main

### What We Value in PRs

- **Small and focused:** One logical change per PR
- **Tested:** New behavior has tests. Bug fixes include regression tests.
- **Clean history:** Meaningful commit messages. Squash noise before submitting.
- **No unrelated changes:** Don't refactor surrounding code in a bug fix PR

## Project Structure

```
CorridorKey-Runtime/
  .github/              GitHub Actions, issue/PR templates
  cmake/                CMake modules and helpers
  docs/                 Technical specification and guidelines
  include/corridorkey/  Public API headers
  src/
    cli/                CLI entry point (thin consumer of the library)
    core/               Inference engine, device detection
    frame_io/           EXR, PNG, video read/write
    post_process/       Color math, despill, despeckle
  tests/
    unit/               Fast, isolated tests
    integration/        Multi-module tests with real files
    e2e/                Full pipeline tests with real model
    fixtures/           Reference files for tests
  scripts/              Model export and optimization (Python)
  vendor/               Vendored header-only libraries
```

## Getting Help

- Open an issue for bugs or feature requests
- Tag your issue with appropriate labels
- For questions about the architecture, see [docs/SPEC.md](docs/SPEC.md)
- For code standards questions, see [docs/GUIDELINES.md](docs/GUIDELINES.md)
