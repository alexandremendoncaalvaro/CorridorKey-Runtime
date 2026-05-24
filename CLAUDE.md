# AI Agent Rules

This file is read automatically by supported coding agents at the start of
each session. It contains the non-negotiable rules for this repository.

## Project Identity

- **Name:** CorridorKey Runtime
- **Language:** C++20 (no modules)
- **Build:** CMake 3.28+ with vcpkg manifest mode and CMake Presets
- **License:** CC BY-NC-SA 4.0

## Structural Rules

`ARCHITECTURE.md` is the source of truth for the directory layout, layer
boundaries, public API surface, PIMPL/visibility/zero-copy/SIMD/Result
contracts, and the Active ADRs list. Read it before adding new files,
exporting new symbols, or moving code across layers. Do not duplicate its
rules here.

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
  package-adobe | package-runtime | release | regen-rtx-release |
  sync-version`. Never
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
- No dates, version stamps, `DRAFT` markers, or changelogs in general docs;
  ADRs and task records may include date/status fields required by their templates
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

<!-- agentic-managed-skills:start -->

## Skills installed by `agentic`

Generated by `@alexandrealvaro/agentic init`. Do not edit this section by hand — re-running the installer regenerates it. Edit the kit instead: https://github.com/alexandremendoncaalvaro/agentic-development.

| Skill | Invoke | Notes |
| --- | --- | --- |
| `agentic-bootstrap` | `/agentic-bootstrap` | Generate or audit `AGENTS.md` at the repo root. |
| `agentic-philosophy` | _(implicit)_ | Universal agent guardrails (think before coding, verify before claiming done). Auto-loads on non-trivial work. |
| `agentic-architecture` | `/agentic-architecture` | Generate or audit `ARCHITECTURE.md` at the repo root. |
| `agentic-adr` | `/agentic-adr` | Draft a new ADR at `doc/adr/NNNN-<slug>.md`. |
| `agentic-spec` | `/agentic-spec` | Draft a feature spec at `doc/specs/NNNN-<slug>.md` (Spec Kit-aligned mandatory sections). Layer 3 of the five-layer artifact stack. |
| `agentic-task` | `/agentic-task` | Draft a new task at `doc/tasks/NNNN-<slug>.md`. |
| `agentic-audit` | `/agentic-audit` | Read-only drift report comparing AGENTS.md / ARCHITECTURE.md / ADRs against the code. |
| `agentic-review` | `/agentic-review` | Fresh-context code review per WORKFLOW §10 — assemble handoff, return structured findings. |
| `agentic-ground` | `/agentic-ground` | Four-source pre-implementation research (docs / OSS / in-repo / git history) + happy-path synthesis + deviation gate. WORKFLOW §4 + §5. |
| `agentic-next` | `/agentic-next` | State survey + prioritized next-action recommendations across the five-layer artifact stack. Read-only navigation aid (`flutter doctor` pattern). |
| `agentic-spike` | `/agentic-spike` | Staged spike with golden fixtures per WORKFLOW §14. Discovery + fixture + pipeline-with-gates + two-layer evaluation, when the *technique* is uncertain across multiple plausible approaches. |
| `agentic-tdg` | `/agentic-tdg` | Outcome-based prompting per WORKFLOW §9. Ground truth pair + Test Dependency Map + three approaches + single-criterion selection, when the technique is known but the implementation strategy is uncertain. |
| `agentic-design` | `/agentic-design` | Bootstrap `DESIGN.md` from existing tokens (frontend projects). |
| `agentic-subagent` | `/agentic-subagent` | Draft a new Claude Code subagent at `.claude/agents/<name>.md`. |

<!-- agentic-managed-skills:end -->

<!-- agentic-managed-skills:start -->

## Skills installed by `agentic`

Generated by `@alexandrealvaro/agentic init`. Do not edit this section by hand — re-running the installer regenerates it. Edit the kit instead: https://github.com/alexandremendoncaalvaro/agentic-development.

| Skill | Invoke | Notes |
| --- | --- | --- |
| `ad-bootstrap` | `/ad-bootstrap` | Generate or audit `AGENTS.md` at the repo root. |
| `ad-philosophy` | _(implicit)_ | Universal agent guardrails (think before coding, verify before claiming done). Auto-loads on non-trivial work. |
| `ad-architecture` | `/ad-architecture` | Generate or audit `ARCHITECTURE.md` at the repo root. |
| `ad-adr` | `/ad-adr` | Draft a new ADR at `doc/adr/NNNN-<slug>.md`. |
| `ad-prd` | `/ad-prd` | Lazy lifecycle owner of `doc/product/PRD.md` (or `doc/product/<slug>.md` multi-product). Layer 3 — product-level scope (target user, problem, success metrics, multi-feature roadmap) that feature specs inherit from. Distinct from `ad-spec` (feature-level). |
| `ad-guidelines` | `/ad-guidelines` | Lazy lifecycle owner of `GUIDELINES.md` (Layer 1 Constitution, full engineering reference). Twelve sections — design principles, code standards, complexity, API, performance, build, static analysis, quality gates, testing, git, documentation, security. Pre-suggested defaults from canon + scan-first detection. |
| `ad-spec` | `/ad-spec` | Draft a feature spec at `doc/specs/NNNN-<slug>.md` (Spec Kit-aligned mandatory sections). Layer 4 of the six-layer artifact stack. References parent PRD (`ad-prd`, Layer 3) for product-scope inheritance. |
| `ad-task` | `/ad-task` | Draft a new task at `doc/tasks/NNNN-<slug>.md`. |
| `ad-audit` | `/ad-audit` | Read-only drift report comparing AGENTS.md / ARCHITECTURE.md / ADRs against the code. |
| `ad-review` | `/ad-review` | Two-axis fresh-context code review per WORKFLOW §10 — Standards (binding docs) and Spec (originating task / spec / PRD) run as parallel sub-agents and report side-by-side. Adapted from mattpocock/skills `review` and bound to the kit's six-layer artifact stack. |
| `ad-ground` | `/ad-ground` | Four-source pre-implementation research (docs / impl-refs / in-repo / git history) + happy-path synthesis + deviation gate. WORKFLOW §4 + §5. |
| `ad-next` | `/ad-next` | State survey + prioritized next-action recommendations across the six-layer artifact stack. Read-only navigation aid (`flutter doctor` pattern). |
| `ad-archive` | `/ad-archive` | Hard-delete completed plan files (tasks / specs / PRDs / superseded ADRs) into git history. ADR-accepted requires absorption proof. |
| `ad-spike` | `/ad-spike` | Staged spike with golden fixtures per WORKFLOW §14. Discovery + fixture + pipeline-with-gates + two-layer evaluation, when the *technique* is uncertain across multiple plausible approaches. |
| `ad-tdg` | `/ad-tdg` | Outcome-based prompting per WORKFLOW §9. Ground truth pair + Test Dependency Map + three approaches + single-criterion selection, when the technique is known but the implementation strategy is uncertain. |
| `ad-tdd` | `/ad-tdd` | Test-Driven Development per WORKFLOW §16. Red-green-refactor as deterministic LLM guardrail. Five phases — confirm regime, plan, tracer bullet, incremental loop, refactor. Tests verify behavior through public interfaces. Horizontal slicing rejected. |
| `ad-domain` | `/ad-domain` | Lazy lifecycle owner of `CONTEXT.md` (Layer 2 — ubiquitous language per Evans 2003). Captures canonical project-specific nouns with aliases-to-avoid, relationships, and flagged ambiguities. Single-context or `CONTEXT-MAP.md` multi-context. |
| `ad-grill` | `/ad-grill` | Interview-before-research grilling session — one question at a time with recommendation, codebase-first, sharpens vocabulary against `CONTEXT.md`, captures terms via `ad-domain` and decisions via `ad-adr` (three-criteria rule). Upstream of `ad-ground`. |
| `ad-deepen` | `/ad-deepen` | Surface deepening opportunities using WORKFLOW §8 vocabulary (Module / Interface / Depth / Seam / Adapter / Leverage / Locality). Three phases — explore, present numbered candidates with deletion-test framing, grill the chosen one. Pairs with `ad-audit`. Profile-scoped to `team` and `mature` only. |
| `ad-diagnose` | `/ad-diagnose` | Disciplined diagnosis loop for hard bugs and performance regressions per WORKFLOW §15. Five phases — build a feedback loop (the skill itself), reproduce, hypothesise (3-5 ranked falsifiable), instrument (one variable at a time), fix + regression-test. |
| `ad-commit` | `/ad-commit` | Atomic Conventional Commits with DCO `Signed-off-by` sign-off. Four phases — scope intake, stage-split when concerns mix, draft message in Conventional Commits format, sign + write. Helper posture, not blocker. |
| `ad-pr` | `/ad-pr` | Open a GitHub pull request with a uniform body shape (Summary / Test plan / Links). Four phases — preflight (`gh` auth + branch pushed), scope assembly, draft body, open + report URL. Title format = Conventional Commits. |
| `ad-merge` | `/ad-merge` | Evaluate and merge a GitHub pull request. Four phases — preflight, evaluate (CI / fresh-context review / linked task / unresolved comments / mergeability), decision (CI green = hard gate; others = warnings), merge with auto-detected mode + `--delete-branch`. |
| `ad-handoff` | `/ad-handoff` | Compact current session into a handoff doc in the OS temp dir. Captures live state, references artifacts by path (no duplication), suggests next skills, redacts secrets. Ephemeral by design — never commits to the repo. |
| `ad-design` | `/ad-design` | Bootstrap `DESIGN.md` from existing tokens (frontend projects). |
| `ad-subagent` | `/ad-subagent` | Draft a new Claude Code subagent at `.claude/agents/<name>.md`. |

<!-- agentic-managed-skills:end -->
