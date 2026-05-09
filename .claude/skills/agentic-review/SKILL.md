---
name: agentic-review
description: Fresh-context code review per WORKFLOW §10 — assemble the diff plus the relevant spec slice (AGENTS.md, applicable ADRs, the task's Acceptance Criteria), delegate to a bundled fresh-context-reviewer subagent with no inherited context, return a structured findings list. Use when the user wants to review a diff, branch, PR, or recent commits against the project's spec, audit for bugs / coupling / edge cases / spec drift, or run a §10 senior-reviewing-junior pass. Adversarial framing — never emits an "approve" verdict.
allowed-tools: Read, Glob, Grep, Bash, Task
---

# /agentic-review

Implements WORKFLOW §10 (Reviewer With Fresh Context) end-to-end. The current session is biased about the code it produced — the same reasoning that wrote it defends it. This skill assembles a clean handoff and delegates review to a subagent that starts with no history.

## Step 0 — Scope the review

Confirm what to review. Default scopes, in priority order:

1. User-named ref or PR (`/agentic-review main..HEAD`, `/agentic-review PR#42`, `/agentic-review <commit-sha>`).
2. Current branch vs `main` (`git diff main...HEAD`).
3. Working-tree changes (`git diff` plus `git diff --staged`).

If no diff exists, stop and tell the user — there's nothing to review.

## Step 1 — Assemble the handoff

The reviewer subagent will get **only** what you assemble here. No conversation history, no prior context.

1. **Diff** — the output of `git diff <range>` for the chosen scope. Use `git diff <range> --stat` first to size the change; if it spans >50 files, ask the user to narrow scope.
2. **AGENTS.md** at the repo root, if present.
3. **ARCHITECTURE.md** at the repo root, if present.
4. **Relevant ADRs** under `doc/adr/` — read every ADR with `Status: accepted` whose subject is touched by the diff. When in doubt, include rather than skip.
5. **Relevant task file** under `doc/tasks/` — if the diff or recent commit messages reference `Task NNNN` or `0NNN-`, read that task's Acceptance Criteria and Plan sections.
6. **Recent commit messages** for the range (`git log <range> --format=%B`) — they often carry the *why*.

Build the handoff as a single message: a short framing paragraph (what's being reviewed, what spec applies), followed by the diff and the spec slice. No prose summary of what you think the diff does — the reviewer reads the diff itself.

## Step 2 — Delegate to the subagent

Invoke the bundled `fresh-context-reviewer` subagent via the `Task` tool. Pass the assembled handoff as the prompt. The subagent has read-only tools (`Read, Glob, Grep, Bash`) and no write access — it cannot accidentally modify the code under review.

## Step 3 — Surface findings

Relay the subagent's findings to the user verbatim, grouped by severity (Blocker / Concern / Note). Do **not** add commentary defending the code. Do **not** synthesize an "approve" verdict — §10 frames the reviewer as adversarial; approval is the user's call after weighing the findings.

If the subagent reports zero findings across all severities, say so explicitly ("no issues found in <range>"). Empty results are real signal, not a gap.

## Output contract

A structured findings list grouped Blocker / Concern / Note, each finding `file:line: <severity>: <problem>. <fix>.`. No "approve" verdict, no defending of the code, no rewrite of the diff. Empty result is reported explicitly.
