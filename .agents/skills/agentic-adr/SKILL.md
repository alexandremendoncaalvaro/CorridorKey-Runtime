---
name: agentic-adr
description: Draft a new ADR (Architecture Decision Record) at doc/adr/NNNN-<short-title>.md, using Michael Nygard's Context/Decision/Consequences/Alternatives pattern. Use when the user wants to record, write, draft, propose, or document an architecture decision. Status starts at proposed; the agent does not flip to accepted without explicit user approval.
allowed-tools: Read, Write, Glob, Bash
---

# /agentic-adr

Drafts `doc/adr/NNNN-<short-title>.md` for one architecture decision. Status lifecycle: `proposed` → `accepted` → `deprecated` | `superseded by ADR-NNNN`.

## Step 1 — Determine NNNN

List `doc/adr/`. NNNN = next available 4-digit number after the highest existing. If `doc/adr/` does not exist, create it; start at `0001`.

## Step 2 — Confirm scope

The ADR captures **one** decision. If the user's request implies multiple decisions, ask which one to write first; the others become follow-up ADRs.

## Step 3 — Fill from conversation only

Use the template below. Fill `Context`, `Decision`, `Consequences`, and `Alternatives Considered` from this conversation only — **no fabrication**, no inferring beyond what the user said. If a section has no signal, ask one question per gap rather than guessing.

* `Decision` must be a directive ("We will…"), not a description.
* `Consequences` lists positive and negative; do not balance for the sake of balance.
* `Alternatives Considered` lists each rejected option with a one-line reason.

## Step 4 — Write the file

Path: `doc/adr/<NNNN>-<short-slug>.md`. Slug: kebab-case, ≤6 words, derived from the decision title. Status: `proposed`. Date: today, ISO format. Deciders: ask the user.

Stop after writing. Do **not** flip status to `accepted` — that requires user review.

## Template — `doc/adr/NNNN-<slug>.md`

````markdown
# ADR-NNNN: `<short imperative title>`

**Status:** `<proposed | accepted | deprecated | superseded by ADR-NNNN>`
**Date:** `<YYYY-MM-DD>`
**Deciders:** `<names or roles>`

## Context

`<What is the issue motivating this decision? What forces are at play — technical, organizational, regulatory, cost?>`

## Decision

`<State as a directive: "We will…". One decision per ADR.>`

## Consequences

`<What becomes easier, harder, or different. List positive, negative, and neutral consequences.>`

## Alternatives Considered

* `<option>` — `<why rejected>`
* `<option>` — `<why rejected>`
````

## Output contract

A single new file at `doc/adr/<NNNN>-<short-slug>.md`. Status `proposed`. No existing ADRs modified. No invented content.
