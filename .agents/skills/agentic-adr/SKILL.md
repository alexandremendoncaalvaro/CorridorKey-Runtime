---
name: agentic-adr
description: Draft a new ADR (Architecture Decision Record) at doc/adr/NNNN-<short-title>.md, using Michael Nygard's Context/Decision/Consequences/Alternatives pattern. Use when the user wants to record, write, draft, propose, or document an architecture decision. Status starts at proposed.
---

<background_information>
Drafts `doc/adr/NNNN-<short-title>.md` for one architecture decision. Status lifecycle: proposed → accepted → deprecated | superseded by ADR-NNNN.
</background_information>

<instructions>
Step 1 — determine NNNN. List `doc/adr/`. NNNN = next available 4-digit number after the highest existing. If `doc/adr/` does not exist, create it; start at 0001.

Step 2 — confirm scope. The ADR captures one decision. If the user's request implies multiple, ask which to write first; the others become follow-up ADRs.

Step 3 — fill from conversation only. Use the template below. Fill Context, Decision, Consequences, and Alternatives Considered from this conversation only — no fabrication. If a section has no signal, ask one question per gap.

`Decision` must be a directive ("We will…"), not a description.
`Consequences` lists positive and negative; do not balance for the sake of balance.
`Alternatives Considered` lists each rejected option with a one-line reason.

Step 4 — write the file. Path: `doc/adr/<NNNN>-<short-slug>.md`. Slug: kebab-case, ≤6 words. Status: proposed. Date: today, ISO format. Deciders: ask the user.

Stop after writing. Do NOT flip status to accepted — that requires user review.
</instructions>

<template path="doc/adr/NNNN-<slug>.md">
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
</template>

<output_contract>
A single new file at `doc/adr/<NNNN>-<short-slug>.md`. Status proposed. No existing ADRs modified. No invented content.
</output_contract>
