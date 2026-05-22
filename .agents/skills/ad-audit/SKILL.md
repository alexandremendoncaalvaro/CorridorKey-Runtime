---
name: ad-audit
description: Read-only drift audit — compare AGENTS.md, ARCHITECTURE.md, and ADR statuses against what the code actually does. Outputs a drift list, never writes files. Use when the user wants to audit, review for drift, sanity-check, or report inconsistencies between the repo's docs and its code.
summary: Read-only drift report comparing AGENTS.md / ARCHITECTURE.md / ADRs against the code.
---

<background_information>
Read-only. Produces a drift list comparing the repo's operational docs against what the code actually does. Writes nothing — the user decides whether to fix the spec or the code.
</background_information>

<instructions>
Step 1 — decide what to audit. If the user names an artifact (AGENTS.md, ARCHITECTURE.md, ADRs), audit only that. Otherwise audit all three categories below.

Step 2 — run checks.

AGENTS.md drift (if present):
- Stack — does the listed stack match `package.json` / `pyproject.toml` / `Cargo.toml` / `go.mod` / equivalent?
- Setup/build/test commands — do they match `package.json#scripts`, `Makefile`, or `pyproject.toml`?
- Quality gates — do referenced hook configs exist (`.husky/`, `.pre-commit-config.yaml`, `.github/workflows/`)?
- Repository layout — do referenced directories exist?
- Pre-approved commands — anything in the list missing from the toolchain?

ARCHITECTURE.md drift (if present):
- Layers and boundaries — do the named directories exist? Sample 1–2 files per layer; are imports respecting the stated boundaries?
- Patterns — sample one handler / one repository / one entry point. Do they follow the documented pattern?
- No `## Active ADRs` section — ARCHITECTURE.md must not duplicate the `doc/adr/` directory index per ADR-0030 §2.

ADR drift (if `doc/adr/` exists):
- Numbering — gaps or duplicates in `doc/adr/NNNN-*.md`?
- Status field — every ADR has one of `proposed | accepted | deprecated | superseded by ADR-NNNN`.
- Superseded chains — every "superseded by ADR-NNNN" target exists.

Spec drift (if `doc/specs/` exists; structural integrity only — does NOT deep-audit spec text against code, deferred per ADR-0011):
- Numbering — gaps or duplicates in `doc/specs/NNNN-*.md`?
- Status field — every spec has one of `draft | accepted | shipped | superseded by SPEC-NNNN`.
- Superseded chains — every "superseded by SPEC-NNNN" target exists.
- Reciprocity — every task with non-empty `Spec ref` points to a spec that exists; every accepted/shipped spec has at least one entry in its Related → Tasks list.
- No checkbox UI — per ADR-0030 §1, Spec is decision-record (not tracking). Functional Requirements / Non-functional Requirements / Success Criteria must use plain bullets, not `- [ ]` checkboxes; implementation tracking lives in per-Spec tasks.
- Status / task aggregate alignment — when every task referencing a spec is done, the spec's Status should be `shipped`. A spec with all tasks done but Status: accepted is drift between work-unit completion and feature-level claim.

Documentation discipline drift (`WORKFLOW.md` §2 / ADR-0008). Audit narrative documents — `README.md`, `AGENTS.md` / `CLAUDE.md`, `ARCHITECTURE.md`, `DESIGN.md`, any prose page under `doc/` that is not a decision-record artifact under `doc/adr/` or `doc/tasks/`:
- Emoji — any present? Rule 3 forbids emoji anywhere (docs, code, comments, commits, skill outputs).
- Dates / version stamps / `DRAFT` markers / changelog blocks in narrative documents — Rule 2 forbids these. Decision-record artifacts under `doc/adr/` and `doc/tasks/` are exempt.
- Business context first — does the first paragraph answer *why* the document exists, before *what* and *how*? Rule 4.
- Scope duplication — does the document copy material that is canonically owned by another file? Rule 5 requires linking, not copying.
- Speculation — phrases like "we might", "in the future", "could be added", or roadmaps without an ADR / task reference. Rule 1 forbids unfounded plans.

Source code (sample, not exhaustive — flag findings, not every match):
- Orphan `TODO` / `FIXME` — Rule 7. A reference to a GitHub Issue or a `doc/tasks/NNNN-*.md` task file makes it not orphan.
- Commented-out code blocks — Rule 7. Removed code lives in git history.

Single-responsibility drift (ADR-0030 / WORKFLOW §2 rules #9–#12):
- Definition-layer tracking UI (Rule #9) — grep `^- \[ \]` / `^- \[x\]` inside AGENTS.md, WORKFLOW.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md, `doc/product/*.md`. Definition documents must not carry per-item checkbox UI. Fenced code-block examples (PR-body templates, etc.) are illustrative, not pillar tracking.
- Directory-as-index duplication (Rule #10) — flag sections that re-state another layer's index: `## Active ADRs` inside ARCHITECTURE.md or AGENTS.md; multi-bullet `## Architectural Principles` digests paraphrasing each ADR; PRD `## Related → ADRs` bullet lists enumerating the kit's ADR ledger.
- Kit-state in WORKFLOW.md (Rule #12) — grep `ADR-[0-9]{4}` in WORKFLOW.md. Universal philosophy must not cite kit-specific ADR numbers (downstream installs lack `doc/adr/`). Literature citations and generic `doc/adr/` references are allowed.
- Cross-references that are decoration (Rule #11) — sample inline `per ADR-NNNN` refs in narrative documents and apply the load-bearing test: deletion leaves the surrounding statement intact → decoration; flag.

Step 3 — output. One line per finding, formatted:
`[file or section]: spec says X, code says Y. Suggested resolution: change spec / change code / discuss.`

Group by artifact. If a category has no drift, print one line: `AGENTS.md — no drift.` etc. If an audited artifact does not exist, say so explicitly rather than reporting zero findings. The Documentation discipline drift category groups findings under `Documentation discipline — <category>: ...`.

If something the user says contradicts what the code shows, surface the conflict. Don't silently trust the user; don't silently trust the code.
</instructions>

<output_contract>
A drift list, no file written. Read-only operation. Empty result is reported explicitly ("no drift found across audited artifacts"), not silently. Missing artifacts are flagged, not skipped.
</output_contract>

## Next

- Address each finding with one of the three resolutions named in the format ("change spec / change code / discuss").
- For findings that require implementation: `/ad-task` to scaffold the fix.
- For workflow drift (where am I, what's stuck): `/ad-next`.
- For kit-version drift (state file behind current kit): `agentic update`.
