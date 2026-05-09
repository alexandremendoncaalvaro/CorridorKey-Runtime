# ADR-0001: Agentic Repository Layout

## Status

accepted

## Context

CorridorKey is maintained with both Claude Code and Codex workflows. Agentic
skills need predictable discovery paths that do not depend on one tool's local
configuration, and architecture/task records need stable locations that survive
context resets.

## Decision

Use root `ARCHITECTURE.md` for structural rules and root `DESIGN.md` for
frontend design rules. Keep ADRs in `doc/adr/` and agentic task records in
`doc/tasks/`. Store Claude Code targets under `.claude/skills/agentic-*` and
`.claude/agents/`. Mirror Codex targets under `.agents/skills/agentic-*` with
each skill carrying `SKILL.md` and `agents/openai.yaml`.

## Consequences

Agents can discover the same project rules from conventional paths. The root
documents are the only architecture and design entry points. Local Claude
settings and worktrees stay ignored.

## Alternatives

Keep all architecture and frontend documentation under `docs/`; rejected
because the agentic architecture and design conventions expect root-level
documents.
