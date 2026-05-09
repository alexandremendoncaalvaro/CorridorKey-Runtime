# Task-0001: Reorganize Agentic Layout

## Status

- [ ] proposed
- [ ] active
- [x] completed

## Goal

Align the repository with the agentic layout used by Claude Code and Codex:
root architecture/design docs, `doc/adr`, `doc/tasks`, `.claude` targets, and
`.agents` targets.

## Scope

- Promote architecture and design docs to root-level conventional paths.
- Add ADR and task record directories.
- Track agentic Claude Code skills and a fresh-context reviewer agent.
- Mirror agentic skills into Codex-facing `.agents/skills`.

## Notes

- This task records the repository layout change only; it does not alter
  runtime source behavior.
