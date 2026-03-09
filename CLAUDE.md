# CLAUDE.md — Project Rules for AI-Assisted Development

This file is read automatically by Claude Code at the start of every session.
It contains the non-negotiable rules for this project.

## Project Identity

- **Name:** CorridorKey Runtime
- **Language:** C++20 (no modules)
- **Build:** CMake 3.28+ with vcpkg manifest mode and CMake Presets
- **License:** CC BY-NC-SA 4.0

## Structural Rules (enforced — see docs/ARCHITECTURE.md for details)

- Public headers go in `include/corridorkey/` — this is the external API
- Use **PIMPL Pattern** for main classes (like `Engine`) to ensure ABI stability
- Use **Symbol Visibility (hidden by default)** — only export via `CORRIDORKEY_API` macro
- **Zero-Copy Performance:** Use `std::span` (via `Image`) for processing and `ImageBuffer` for ownership. NEVER use `std::vector<float>` for large pixel data.
- **SIMD Alignment:** Ensure 64-byte alignment for all image allocations.
- Implementation goes in `src/` subdirectories by domain:
  - `src/cli/` — CLI only (main + arg parsing, no business logic)
  - `src/common/` — shared internal utilities (STL-only, no external deps)
  - `src/core/` — inference engine, device detection, ONNX Runtime wrapper
  - `src/frame_io/` — EXR, PNG, video I/O (OpenEXR, stb, FFmpeg confined here)
  - `src/post_process/` — pure pixel math (color utils, despill, despeckle)
- Tests go in `tests/unit/`, `tests/integration/`, or `tests/e2e/` by level
- External library types (OrtSession, Imf::*, AVFrame, etc.) NEVER appear in
  public headers — they are wrapped in `src/`
- Do NOT create new top-level directories or new `src/` subdirectories without
  updating docs/ARCHITECTURE.md

## Code Standards (see docs/GUIDELINES.md for details)

- `snake_case` for functions, variables, files
- `PascalCase` for types (classes, structs, enums)
- `m_` prefix for private members
- `UPPER_SNAKE_CASE` for constants
- `.hpp` for headers, `.cpp` for implementation — no `.h` files
- Const correctness everywhere
- RAII for all resources — no raw new/delete
- Error handling via `std::expected` or `std::optional`, not exceptions for
  expected failures
- Max cognitive complexity per function: 15
- Max ~200 lines per .cpp, ~100 lines per .hpp (guideline, not hard rule)
- Prefer early returns over nested if/else
- No abbreviations in names (except standard: rgb, exr, fps, io)

## Testing

- Framework: Catch2 v3
- Tags: `[unit]`, `[integration]`, `[e2e]`, `[regression]`
- Bug fixes MUST include a regression test
- Unit tests: no I/O, no GPU, < 1 second each
- Test file naming: `test_<module>.cpp`

## Build & Infrastructure

- Use `CMakePresets.json` as the source of truth for all build configurations
- Always use **vcpkg Manifest Mode** with `vcpkg-configuration.json` for baseline pinning
- Strict Warnings: `-Wall -Wextra -Wpedantic -Werror` (or MSVC equivalent)
- Enable **AddressSanitizer (ASAN)** in Debug presets
- No global CMake commands (target-based approach only)

## Commit Style

- Conventional Commits: `feat:`, `fix:`, `test:`, `refactor:`, `chore:`,
  `docs:`, `perf:`
- Do not commit to `main` directly — use feature branches

## Documentation Rules (see docs/GUIDELINES.md section 6 for details)

- Documentation contains **definitions and decisions**, not speculation,
  history, or unfounded plans
- No dates, version stamps, "DRAFT" markers, or changelogs in docs. Git
  handles history
- No emoji anywhere: not in docs, not in code, not in commits
- Every document starts with business context (why) before technical details
- Each document has one scope. Do not duplicate content across documents
- Code is the primary documentation of how things work. Comments in code
  explain **why** (intent, constraints, trade-offs), never **what** (which
  the code itself shows)
- No commented-out code, no TODO/FIXME in source. Use GitHub Issues
- Tests are living documentation of behavior. A well-named test documents a
  requirement and is verified on every build

## What NOT to Do

- Do not use `std::vector` for image data (use `ImageBuffer`).
- Do not use expensive math functions (`std::pow`, `std::exp`) inside hot pixel loops (use LUTs).
- Do not add files to root unless they are project-level config
- Do not put business logic in `src/cli/`
- Do not leak external library types into `include/corridorkey/`
- Do not create documentation files unless explicitly asked
- Do not bypass pre-commit hooks with --no-verify
- Do not commit model files (.onnx, .pth) — they are gitignored
- Do not add dependencies without a `$comment` in vcpkg.json
- Do not put source code in documentation files
- Do not write comments that restate what the code does
- Do not use emoji in any project artifact
- Do not use `std::exit` or `abort` in the library; return `Result<T>` instead
- Do not use global state or static variables in the library
