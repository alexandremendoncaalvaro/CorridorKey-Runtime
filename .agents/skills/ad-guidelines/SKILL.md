---
name: ad-guidelines
description: Draft or update `GUIDELINES.md` at the repo root — the project's full engineering reference. Layer 1 Constitution trinity member (alongside `WORKFLOW.md` universal philosophy and `AGENTS.md` distilled rules). Covers Clean Architecture binding, SOLID application, Object Calisthenics tier (loose / moderate / strict per Bay 2008), naming conventions, error handling, complexity discipline, API rules, performance standards, build system, static analysis, quality gates, testing strategy, git workflow, documentation, and security. Scan-first — reads language toolchain, existing test/lint/format config, and existing AGENTS.md sections, pre-fills every placeholder it can verify, then asks only the genuine gaps and preference questions. Use when the user wants to draft, scaffold, audit, or update engineering guidelines / code standards / coding conventions / SOLID adoption / Object Calisthenics / testing strategy / security policy / build conventions for the project. Skill is lazy — file only exists when the project needs engineering standards.
summary: Lazy lifecycle owner of `GUIDELINES.md` (Layer 1 Constitution, full engineering reference). Twelve sections — design principles, code standards, complexity, API, performance, build, static analysis, quality gates, testing, git, documentation, security. Pre-suggested defaults from canon + scan-first detection.
---

<background_information>
Layer 1 Constitution trinity member. Lazy lifecycle owner of `GUIDELINES.md` at the repo root. Companion to `WORKFLOW.md` (universal philosophy, kit-shipped) and `AGENTS.md` (distilled non-negotiable rules read every session).

`GUIDELINES.md` is the full reference; `AGENTS.md` is the distilled summary. Each AGENTS.md section that has detail points to the corresponding GUIDELINES.md section. The two never duplicate — `ad-audit` flags duplication as drift.

Skill is excluded from `poc` profile. Universal at `solo` / `team` / `mature`.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions engineering guidelines, code standards, SOLID, Object Calisthenics, testing strategy, or security policy, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when the user wants to scope or update project-level engineering standards: new project past poc, existing rules buried in AGENTS.md, stack change, Object Calisthenics tier review.

Route elsewhere when:

- Scope is system structure → `ad-architecture`.
- Scope is product → `ad-prd`.
- Scope is vocabulary → `ad-domain`.
- Scope is one binding decision → `ad-adr`.
- Scope is wiring the gates → `ad-hooks`.

Step 1 — codebase-first scan. Before any preference question, detect:

Language and toolchain:
- `package.json` → JS/TS; read engines, scripts, deps, devDeps.
- `Cargo.toml` → Rust; read edition, deps, dev-dependencies.
- `pyproject.toml` / `setup.cfg` / `requirements.txt` → Python.
- `CMakeLists.txt` / `CMakePresets.json` / `vcpkg.json` → C/C++.
- `go.mod` → Go.
- `Gemfile`, `pom.xml`, `build.gradle`, `composer.json` for other stacks.

Test framework: jest/vitest/playwright/mocha configs, pytest, cargo `[dev-dependencies]`, catch2/gtest in CMake, `_test.go` files.

Lint and format: `.eslintrc.*`, `.prettierrc.*`, `.rustfmt.toml`, `clippy.toml`, `.ruff.toml`, `mypy.ini`, `.clang-format`, `.clang-tidy`, `.golangci.yml`.

Hooks and CI: `.husky/`, `lefthook.yml`, `.pre-commit-config.yaml`, `.githooks/`, `.github/workflows/`, `.gitlab-ci.yml`, `.circleci/`.

Existing standards:
- `AGENTS.md` `## Code Style` and `## Architectural Principles` — extract into GUIDELINES, replace with pointers.
- `ARCHITECTURE.md` `## Source Tree` and `## Dependency Rules` — informs Clean Architecture §1.1.
- `doc/adr/` — binding decisions about error handling, naming, DI — carry forward.
- `CONTEXT.md` — domain vocabulary the guidelines must respect.

Profile: read `.claude/agentic-state.json` / `.agents/agentic-state.json`. Profile shapes strictness defaults (mature → strict Object Calisthenics by default; solo → loose).

Only after the scan produces no answer does the skill ask. Asking about something the repo already states wastes the user's attention.

Step 2 — pre-fill the template. Open `templates/guidelines.md`. Fill detected fields before any interview:

- §2.1 naming conventions table per language idiom.
- §2.2 error handling per language idiom.
- §6.1 toolchain + version + source-of-truth config file.
- §6.2 dependency manager from detected manifest.
- §7 static-analysis tools detected.
- §8 quality gates from hooks scan; mark `<not yet wired — invoke /ad-hooks>` when absent.
- §9.2 unit-test framework detected.
- §9.6 tag taxonomy kit defaults.
- §10 git workflow kit pattern (Conventional Commits, DCO sign-off via `ad-commit`).
- §11.1 document scope table kit-standard.
- §12 security boundaries from scan.

Sections not applicable to the project (e.g., §5 Performance Standards for a CRUD app) are omitted from the file, not left as `<TODO>`.

Step 3 — interview to fill preference questions. One question at a time. Skip those already answered by the scan.

3.1 Project tradeoff statement. One sentence. Suggest from signals; user confirms or supplies.

3.2 Object Calisthenics tier. Three-choice. Default per profile (poc N/A; solo loose; team moderate; mature strict).

- Loose: rules 6, 7 relaxed, 1 as guideline.
- Moderate: loose + rules 2, 3, 4.
- Strict: all nine rules including 5, 8, 9.

3.3 Complexity caps. Confirm defaults: cognitive complexity 15, function ~50 lines, file ~200 lines, indentation ≤3.

3.4 Performance standards. Ask: perf budget? If no → skip §5 entirely. If yes → hot-path rules, baseline location, regression threshold.

3.5 Security posture. Untrusted input sources, secret-handling pattern, dependency-audit tool.

3.6 Documentation extensions. Default references WORKFLOW §2 + ad-philosophy. Ask only for project-specific extensions.

Codex has no `AskUserQuestion` primitive — use inline numbered text for the three-tier Object Calisthenics question and other multi-choice prompts.

Step 4 — AGENTS.md reciprocity. After writing `GUIDELINES.md`, offer to refresh `AGENTS.md` engineering sections as pointer stubs:

```
## Code Standards

See `GUIDELINES.md` §2 for the full reference. Non-negotiable subset:
- <language naming line>
- <error handling pattern line>
- <no commented-out code, no orphan TODO line>
```

User confirms before the AGENTS.md rewrite. Never modify AGENTS.md silently.

Step 5 — write the file. Path: `GUIDELINES.md` at repo root. Use the template at `templates/guidelines.md`. Sections the user skipped do not land. Sections that were fully pre-filled land verbatim. Sections requiring preference land with the user's answer. Stop after writing; print a one-line summary.

Step 6 — editing guidance for later turns. Tier change → update §3.1 checked rules; never delete the rule list (audit trail). New language → expand §2.1 table; do not rewrite existing rows. New static-analysis tool → add row to §7. Perf budget change → update §5; never delete previous without recording rationale. Documentation extensions → append to §11.1 table. Never rewrite existing prose — append rationale paragraphs.
</instructions>

<output_contract>
- Primary output: `GUIDELINES.md` at the repo root.
- Side-effect (optional, user-confirmed): `AGENTS.md` engineering sections rewritten as pointer stubs to `GUIDELINES.md`.
- No dates inside narrative prose; the file is project-state, not a decision-record artifact.

Next: invoke `ad-hooks` to wire the quality gates this file describes (team/mature); invoke `ad-bootstrap` to refresh AGENTS.md with pointer-style sections; invoke `ad-adr` when a binding decision arises the guidelines do not yet cover; periodic drift via `ad-audit`.
</output_contract>
