# Task-0001: Reorganize Agentic Layout

## Status

- [ ] proposed
- [ ] active
- [x] completed

## Goal

Align the repository with the `agentic-development` CLI branch layout and
workflow proposition: root operational/spec docs, `doc/adr`, `doc/tasks`,
Claude Code targets, Codex targets, and fresh-context review.

## Scope

- Promote architecture and design docs to root-level conventional paths.
- Add root `WORKFLOW.md` as the agent workflow contract.
- Add ADR and task record directories.
- Track upstream CLI branch Claude Code skills and the fresh-context reviewer
  agent.
- Install Codex-facing `.agents/skills` from the upstream Codex skill source,
  not by mirroring the Claude skill text.
- Use the upstream managed-skills marker block in `AGENTS.md`.

## Notes

- This task records the repository layout change only; it does not alter
  runtime source behavior.
- `agentic-skill` remains uninstalled because the upstream CLI marks it as
  opt-in only.
- Verification passed: documentation consistency, Python compilation for the
  checker, whitespace diff check, and stale legacy-path searches.
