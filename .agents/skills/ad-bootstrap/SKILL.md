---
name: ad-bootstrap
description: Generate AGENTS.md at the repo root by scanning the codebase first, pre-filling placeholders from observed signals, and asking only the genuine gaps. Use whenever the user wants to bootstrap, scaffold, generate, create, set up, or audit AGENTS.md / CLAUDE.md / agents.md (the operational guide for agents working on this project).
summary: Generate or audit `AGENTS.md` at the repo root.
---

<background_information>
Produces `AGENTS.md` at the repo root, ≤150 lines, every line operational. Generic agent behavior (think-before-coding, verify-before-claiming-done, etc.) does NOT belong here — that lives in the `ad-philosophy` skill.

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
- `GUIDELINES.md` at repo root — if present, the AGENTS.md sections that have a corresponding GUIDELINES.md section (Code Style → §2; Quality Gates → §8; Commit & PR → §10; Security & Privacy → §12) emit as pointer stubs instead of inline rules' ' reciprocity and ADR-0030 §1.
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

<when-no-guidelines>
Deterministic enforcement — agent cannot skip.

* Pre-commit hook (fast): `<lint, format, secret-scan>`
* Pre-push hook (thorough): `<build + unit + integration>`
* Visual/E2E for UI (if applicable): `<e.g., Cypress, Playwright, Claude in Chrome — leave blank for non-UI projects>`
* Hook config lives in: `<.husky/, .pre-commit-config.yaml, .claude/settings.json — see code.claude.com/docs/en/hooks>`
* CI blocks on: `<list>`
</when-no-guidelines>

<when-guidelines-md-exists>
See `GUIDELINES.md` §8 for the full reference. Non-negotiable subset:

* `<distilled pre-push hook line>`
* `<distilled pre-commit hook line — or "intentionally absent" if not wired>`
* Never bypass: no `--no-verify`, no skipped hooks, no deleted failing tests.
</when-guidelines-md-exists>

## Code Style

<when-no-guidelines>
Only what differs from language defaults.

* `<e.g., ES modules, not CommonJS>`
* `<e.g., destructure imports>`
* `<e.g., no `any` outside `internal/types/`>`
* `<e.g., Pydantic for all request/response shapes>`
</when-no-guidelines>

<when-guidelines-md-exists>
See `GUIDELINES.md` §2 for the full reference. Non-negotiable subset:

* `<language-specific naming convention line>`
* `<error-handling pattern line>`
* `<module-surface line — ESM vs CommonJS, named-vs-default exports, etc.>`
</when-guidelines-md-exists>

## Architectural Principles

Binding decisions live in [`doc/adr/`](doc/adr/). Do not reinvent.

## Repository Layout

`<where logic, tests, docs, infra live — only if not obvious from the tree>`
`<.agents/skills/ — list of available Codex skills, if any>`
`<.codex/agents/ — list of custom Codex subagents, if any>`
`<doc/adr/ — list of binding ADRs, if any>`
`<doc/tasks/ — task tracking convention, if used>`

## Commit & PR Conventions

<when-no-guidelines>
* Commits: `<conventional / project-specific>`
* Branches: `<feat/, fix/, chore/>`
* PRs require: `<green CI, one review, linked issue>`
* Never push to `<main>` directly.
</when-no-guidelines>

<when-guidelines-md-exists>
See `GUIDELINES.md` §10 for the full reference. Non-negotiable subset:

* `<commit-format line — Conventional Commits + DCO sign-off>`
* `<branch-strategy line — main / feature-branch policy>`
* Never push to `<main>` directly.
</when-guidelines-md-exists>

## Security & Privacy

<when-no-guidelines>
* Secrets: `<location — never committed>`
* Files the agent must not read or modify: `<list>`
* Data classification: `<e.g., no PII in logs>`
* Pre-approved commands (no prompt): `<e.g., gh, npm test, npm run lint>`
* MCP servers approved: `<list>`
</when-no-guidelines>

<when-guidelines-md-exists>
See `GUIDELINES.md` §12 for the full reference. Non-negotiable subset:

* `<secret-handling line — env files gitignored>`
* `<files-not-to-read line — .env, .npmrc, etc>`
* `<pre-approved commands line — agent-allowed no-prompt list>`
</when-guidelines-md-exists>

## Gotchas

Real traps. Each one should map to an incident or to specific code.

* `<e.g., migrations not idempotent — never edit, always create new>`
* `<e.g., DB is UTC, app displays America/Sao_Paulo>`
</template>

<output_contract>
A single `AGENTS.md` at the repo root, ≤150 lines, every line operational. No "External Resources" section. No appended Universal Agent Behavior block — that lives in the `ad-philosophy` skill. No meta-prose explaining gaps. In audit mode: a drift list, no file written.

`AGENTS.md` is a narrative document, so the Documentation Discipline rules in `WORKFLOW.md` §2 apply at write time:
- No emoji anywhere in the file.
- No dates, version stamps, `DRAFT` markers, or changelog blocks. Stack versions are facts (`Node ≥18`); release timelines are not.
- `Project Overview` is the business-context-first paragraph — *why* the project exists before *what* it does.
- One scope: this file is the operational guide for agents. Do not duplicate `ARCHITECTURE.md` patterns or ADR rationale here; link instead.
- No speculation. If a section has no signal, write `<TODO: not yet wired>` once; do not narrate "this could be added later".
</output_contract>

## Next

- In `team` / `mature`: run `/ad-architecture` once load-bearing patterns emerge in the code.
- When you scope a product (multi-feature, target user, success metrics): `/ad-prd` (Layer 3 of the six-layer artifact stack; excluded from `poc`).
- When you start your first feature: `/ad-spec` (Layer 4 of the six-layer artifact stack; references parent PRD for product-scope inheritance).
- Skip both above in `poc` / `solo` until the project genuinely needs them — the WORKFLOW §1 prune principle applies.
- `ad-philosophy` auto-loads on non-trivial work.
