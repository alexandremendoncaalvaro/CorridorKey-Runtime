# ADR-0001: Agentic Repository Layout

## Status

accepted

## Context

CorridorKey is maintained with both Claude Code and Codex workflows. The
`agentic-development` CLI branch defines separate surfaces for operational
context, canonical specs, on-demand skills, and fresh-context review. Those
surfaces need predictable discovery paths that do not depend on one tool's local
configuration.

## Decision

Use root `ARCHITECTURE.md` for structural rules, root `DESIGN.md` for frontend
design rules, and root `WORKFLOW.md` for the agent workflow proposition. Keep
ADRs in `doc/adr/` and agentic task records in `doc/tasks/`.

Store Claude Code skill targets under `.claude/skills/agentic-*`. Route
manifest-declared Claude subagents to `.claude/agents/`, including
`fresh-context-reviewer.md` for `agentic-review`. Keep `CLAUDE.md` as the
Claude Code import entry point for `AGENTS.md`.

Store Codex skill targets under `.agents/skills/agentic-*`, with each installed
skill carrying the upstream Codex `SKILL.md` and `agents/openai.yaml`. Codex
targets are not mirrors of the Claude skill bodies; they use the Codex/XML-style
prompt surface from the CLI branch.

Install the upstream default skill set for this repository: seven required
skills, `agentic-design` because the repository has a frontend, and
`agentic-subagent` for Claude Code. Leave `agentic-skill` uninstalled unless it
is explicitly opted in.

## Consequences

Agents can discover the same project rules from conventional paths. The root
documents are the only architecture, design, and workflow entry points. The
managed AGENTS section uses the CLI branch markers so reruns can refresh it
without touching authored content. Local Claude settings and worktrees stay
ignored.

## Alternatives

Keep architecture and frontend canonical docs under `docs/`; rejected because
the agentic architecture and design conventions expect root-level documents.

Duplicate the entire `AGENTS.md` body into `CLAUDE.md`; rejected because the
workflow convention allows `CLAUDE.md` to import `AGENTS.md` with `@AGENTS.md`,
which avoids drift while preserving the Claude Code entry point.
