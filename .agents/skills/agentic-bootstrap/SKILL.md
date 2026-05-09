---
name: agentic-bootstrap
description: Generate AGENTS.md at the repo root by scanning the codebase first, pre-filling placeholders from observed signals, and asking only the genuine gaps. Use whenever the user wants to bootstrap, scaffold, generate, create, set up, or audit AGENTS.md / CLAUDE.md / agents.md (the operational guide for agents working on this project).
---

<background_information>
Produces `AGENTS.md` at the repo root, ≤150 lines, every line operational. Generic agent behavior (think-before-coding, verify-before-claiming-done, etc.) does NOT belong here — that lives in the `agentic-philosophy` skill.

Three modes, detected from filesystem state:
- `AGENTS.md` exists at the repo root → audit (do not rewrite, output drift list only)
- `AGENTS.md` absent and only trivial files present (`.git`, `node_modules`, `.gitignore`, `.gitattributes`, `.DS_Store`, `.env*`, `.idea`, `.vscode`, `LICENSE*`, `README.md`) → greenfield (interview by placeholder, no scan)
- `AGENTS.md` absent and meaningful code present → brownfield (scan first, pre-fill, ask only gaps)
</background_information>

<instructions>
Step 0 — detect mode from the filesystem state above. Skip Step 1 unless the mode is brownfield.

Step 1 — scan (brownfield only). Read in this order, taking the first that exists for each category:
- Manifests: `package.json`, `pyproject.toml`, `Cargo.toml`, `go.mod`, `Gemfile`, `composer.json`, `pubspec.yaml`.
- `README.md`, plus any `doc/` or `docs/` directory.
- Top-level directory listing.
- `doc/adr/` — binding decisions; read every ADR.
- `.claude/`, `.cursor/`, `.openai/`, `.agents/` — existing agent config.
- Hook configs: `.husky/`, `.pre-commit-config.yaml`, `.github/workflows/`, `.gitlab-ci.yml`, `.circleci/`.
- Lockfiles: `package-lock.json`, `yarn.lock`, `pnpm-lock.yaml`, `poetry.lock`, `Cargo.lock`.
- `git remote -v` for the repo URL.

Build a model of: stack (languages and versions), entry points, build / test / lint commands, conventions, quality gates, security boundaries, gotchas confirmed by code.

Step 2 — pre-fill. For every `<placeholder>` in the template, fill from observed signals. No fabrication. If a section has no signal, write `<TODO: not yet wired>` in one line and move on. Do not write meta-prose explaining the gap.

Step 3 — show only the gaps. Print to the user:
- (a) placeholders that could not be filled from repo signals;
- (b) signals that conflict (two test commands, two style configs, README contradicts code, etc.).

One question per gap. Skip everything filled confidently. Do NOT ask philosophical questions ("is this doc for agents or humans?", "what is the most important quality bar?") — those are decisions, not interview material.

Step 4 — write the file. On user confirmation, write `AGENTS.md` at the repo root. Cut every line that does not change agent behavior. No "External Resources" section (URLs are derivable from `git remote` and the manifest). No appended Universal Agent Behavior block. No marketing prose.

Audit-mode override: do NOT write the file. Produce a drift list. Format each line as:
`[file or section]: spec says X, code says Y. Suggested resolution: change spec / change code / discuss.`

If something the user says contradicts what the code shows, surface the conflict. Don't silently trust the user; don't silently trust the code.
</instructions>

<template path="AGENTS.md">
# AGENTS.md

## Project Overview

`<one sentence: what it does, who runs it, the constraint a wrong change would violate>`

**Stack:** `<languages + versions, runtimes, frameworks, database>`
**Entry points:** `<main services and where to find them>`

## Setup, Build, Test

```bash
# Install
<command>

# Build
<command>

# Test (single file preferred over full suite)
<single test command>
<full suite command>

# Run before any commit
<lint>
<format>
<typecheck>
```

Document non-obvious flags or env vars inline.

## Quality Gates

Deterministic enforcement — agent cannot skip.

* Pre-commit hook (fast): `<lint, format, secret-scan>`
* Pre-push hook (thorough): `<build + unit + integration>`
* Visual/E2E for UI (if applicable): `<e.g., Cypress, Playwright, Claude in Chrome — leave blank for non-UI projects>`
* Hook config lives in: `<.husky/, .pre-commit-config.yaml, .claude/settings.json — see code.claude.com/docs/en/hooks>`
* CI blocks on: `<list>`

## Code Style

Only what differs from language defaults.

* `<e.g., ES modules, not CommonJS>`
* `<e.g., destructure imports>`
* `<e.g., no `any` outside `internal/types/`>`
* `<e.g., Pydantic for all request/response shapes>`

## Architectural Principles

Decisions the agent must follow, not reinvent.

* `<e.g., Clean Architecture — core logic isolated from frameworks>`
* `<e.g., Repository pattern for all DB access>`
* `<e.g., All HTTP handlers go through middleware in `src/middleware/`>`
* `<e.g., Single responsibility, no `else` chains, low indentation>`

## Repository Layout

`<where logic, tests, docs, infra live — only if not obvious from the tree>`
`<.claude/skills/ — list of available skills, if any>`
`<.claude/agents/ — list of custom subagents, if any>`
`<doc/adr/ — list of binding ADRs, if any>`
`<doc/tasks/ — task tracking convention, if used>`

## Commit & PR Conventions

* Commits: `<conventional / project-specific>`
* Branches: `<feat/, fix/, chore/>`
* PRs require: `<green CI, one review, linked issue>`
* Never push to `<main>` directly.

## Security & Privacy

* Secrets: `<location — never committed>`
* Files the agent must not read or modify: `<list>`
* Data classification: `<e.g., no PII in logs>`
* Pre-approved commands (no prompt): `<e.g., gh, npm test, npm run lint>`
* MCP servers approved: `<list>`

## Gotchas

Real traps. Each one should map to an incident or to specific code.

* `<e.g., migrations not idempotent — never edit, always create new>`
* `<e.g., DB is UTC, app displays America/Sao_Paulo>`
</template>

<output_contract>
A single `AGENTS.md` at the repo root, ≤150 lines, every line operational. No "External Resources" section. No appended Universal Agent Behavior block — that lives in the `agentic-philosophy` skill. No meta-prose explaining gaps. In audit mode: a drift list, no file written.
</output_contract>
