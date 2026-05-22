---
name: ad-spec
description: Draft a feature-level specification at doc/specs/NNNN-<slug>.md following the six-layer artifact stack (Constitution → Domain → Product → Spec → Plan/Decisions → Code). Adapts GitHub Spec Kit's mandatory sections (User Scenarios, Requirements, Success Criteria) to the kit's documentation discipline. Use when the user wants to write, draft, scaffold, or open a feature spec, feature brief, user stories, or success criteria for one feature. Product-level scoping (PRD, multi-feature roadmap, target user, product success metrics) belongs to `ad-prd` (Layer 3). Status starts at draft.
summary: Draft a feature spec at `doc/specs/NNNN-<slug>.md` (Spec Kit-aligned mandatory sections). Layer 4 of the six-layer artifact stack. References parent PRD (`ad-prd`, Layer 3) for product-scope inheritance.
---

<background_information>
Drafts `doc/specs/<NNNN>-<short-slug>.md` for one feature. Status lifecycle: draft → accepted → shipped | superseded by SPEC-NNNN. Spec is the layer-4 artifact in the kit's six-layer stack — Constitution (`AGENTS.md` + `WORKFLOW.md`) → Domain (`CONTEXT.md`) → Product (`doc/product/PRD.md`) → Spec (this skill) → Plan/Decisions (`ARCHITECTURE.md` + `doc/adr/` + `doc/tasks/`) → Code. Multiple tasks implement one spec; ADRs may be driven by spec constraints. The Domain layer (ubiquitous language per Evans 2003) is the source of canonical nouns the spec must use; if the spec introduces a new noun, resolve it through `CONTEXT.md` first. The Product layer (PRD) is the source of target user, product-level success metrics, and cross-feature constraints the spec inherits from.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user asks to "draft a spec" for a single feature, invoke this skill manually. If the user asks for a "PRD" or "product requirements" — route them to `ad-prd` instead; PRDs are Layer 3 (product-level), not feature-level.
</background_information>

<instructions>
Step 1 — determine NNNN and slug. List `doc/specs/`. NNNN = next available 4-digit number after the highest existing (mirrors ADR and task conventions). If `doc/specs/` does not exist, create it; start at 0001. Slug: kebab-case, ≤6 words, derived from the feature title.

Step 2 — confirm scope. The spec captures one feature. If the user's request implies multiple, ask which to write first; the others become follow-up specs. A "feature" here is the smallest user-visible outcome that has its own success criteria — not a task (work unit) and not a binding architectural decision (ADR).

Step 3 — interview to fill. Ask one question per missing field, in this order:
- Context: business context first per ADR-0008. Why the feature exists, the user / constraint / problem it addresses, the cost of not building it.
- User Scenarios: Given-When-Then for the key flows. Each scenario independently testable.
- Functional Requirements: testable statements. Plain bullets (no checkbox — Spec is decision-record per ADR-0030 §1; implementation tracking lives in per-Spec tasks).
- Non-functional Requirements: perf / security / a11y / observability — only when binding.
- Success Criteria: measurable per WORKFLOW.md §1. Plain bullets; pass/fail observable, not aspirational. Per-criterion progress tracking lives in per-Spec tasks.
- Edge Cases: empty inputs, large inputs, concurrent access, missing prerequisites, permission errors.
- Out of Scope: explicit non-goals.
- Open Questions: deferred decisions. Each becomes an ADR or a documented punt.
- Related: ADRs touched, tasks implementing it (lazy), other specs this one supersedes or depends on.

Status starts at draft. Created: today, ISO format. Owner: ask. Do NOT invent values; leave `<TODO>` and ask.

Step 4 — write the file. Path: `doc/specs/<NNNN>-<short-slug>.md`. Use the template below.

Stop after writing. Do NOT flip status to accepted — that requires user review.

Step 5 — editing guidance for later turns. Append to Open Questions; close with resolution paragraphs, never delete. Flip Status to accepted on user sign-off; shipped once all per-Spec tasks complete; superseded when replaced. Add tasks to Related as they are created. Implementation tracking (per-criterion progress) lives in per-Spec tasks, not in the Spec. Never rewrite existing prose — append rationale to Open Questions rather than mutating original requirement text.
</instructions>

<template path="doc/specs/NNNN-<slug>.md">
# Spec `<NNNN>`: `<short imperative title>`

**Status:** `<draft | accepted | shipped | superseded by SPEC-NNNN>`
**Created:** `<YYYY-MM-DD>`
**Owner:** `<name or role>`

## Context

`<Why this feature exists. The user, the constraint, the problem. What would break if this feature did not ship.>`

## User Scenarios

`<Given-When-Then for the key flows. Each scenario independently testable.>`

- **Scenario 1:** `<short title>`
  - Given `<starting state>`
  - When `<action>`
  - Then `<observable outcome>`

## Requirements

### Functional

- `<R1: testable statement>`
- `<R2>`

### Non-functional

- `<perf / security / a11y / observability — only when binding>`

## Success Criteria

Definitional. Measurable conditions; pass/fail observable, not aspirational. Per-criterion progress tracking lives in per-Spec tasks.

- `<criterion 1: measurable, observable>`
- `<criterion 2>`

## Edge Cases

- `<empty input behavior>`
- `<large input / failure modes>`
- `<permission / concurrency / state-restoration cases>`

## Out of Scope

`<Explicit non-goals. Anything readers might assume is in scope but isn't.>`

## Open Questions

`<Deferred decisions. Each becomes an ADR or a documented punt.>`

## Related

- ADRs: `<list with links — written as the spec is accepted>`
- Tasks: `<doc/tasks/NNNN-... — appended as tasks are created>`
- Supersedes / Depends on: `<other spec links if any>`
</template>

<output_contract>
A single new file at `doc/specs/<NNNN>-<short-slug>.md`. Status draft. Tasks list empty (filled lazily as tasks land). No existing specs modified. No invented values.

Spec is a narrative document but is exempt from ADR-0008's no-dates rule for the same reason ADRs and tasks are: the Status lifecycle and Created field are part of the auditability primitive. Remaining documentation discipline rules apply at write time:
- No emoji anywhere in the file.
- Context is the business-context-first section — why the feature exists before what it does.
- One scope: one feature per spec.
- No speculation. Open Questions go in their named section.
- No commented-out requirements or TODO/FIXME — every deferred item references a tracked work item or lives under Open Questions.
</output_contract>

## Next

- `/ad-ground` for the four-source research pass before code (WORKFLOW §4 + §5).
- `/ad-task` to break the spec into work units; each task carries a `Spec ref` field pointing back to this file.
- `/ad-adr` if scoping the spec surfaced a binding architectural decision.
- Flip Status to `accepted` once you sign off and tasks start being created. Flip to `shipped` after release.
