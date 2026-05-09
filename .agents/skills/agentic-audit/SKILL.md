---
name: agentic-audit
description: Read-only drift audit — compare AGENTS.md, ARCHITECTURE.md, and ADR statuses against what the code actually does. Outputs a drift list, never writes files. Use when the user wants to audit, review for drift, sanity-check, or report inconsistencies between the repo's docs and its code.
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
- Active ADRs section — every entry in `doc/adr/` with `Status: accepted` is listed; nothing listed has been superseded.

ADR drift (if `doc/adr/` exists):
- Numbering — gaps or duplicates in `doc/adr/NNNN-*.md`?
- Status field — every ADR has one of `proposed | accepted | deprecated | superseded by ADR-NNNN`.
- Superseded chains — every "superseded by ADR-NNNN" target exists.

Step 3 — output. One line per finding, formatted:
`[file or section]: spec says X, code says Y. Suggested resolution: change spec / change code / discuss.`

Group by artifact. If a category has no drift, print one line: `AGENTS.md — no drift.` etc. If an audited artifact does not exist, say so explicitly rather than reporting zero findings.

If something the user says contradicts what the code shows, surface the conflict. Don't silently trust the user; don't silently trust the code.
</instructions>

<output_contract>
A drift list, no file written. Read-only operation. Empty result is reported explicitly ("no drift found across audited artifacts"), not silently. Missing artifacts are flagged, not skipped.
</output_contract>
