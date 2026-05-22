---
name: ad-grill
description: Interview-before-research grilling session that challenges a fuzzy ask against the existing codebase, sharpens vocabulary against `CONTEXT.md`, and resolves the design tree branch by branch. One question at a time, codebase-first when an answer is in code, with each question carrying a recommended answer. Triggers on "grill me", "interview me", "stress test the plan", "challenge my assumptions", "before implementing", "ask me questions", "fuzzy ask", "sharpen the question", "what should I clarify", "/ad-grill". Routes to `ad-ground` once the question is research-ready, `ad-tdg` once the technique is settled, `ad-spike` for technique discovery, `ad-diagnose` for debugging.
summary: Interview-before-research grilling session — one question at a time with recommendation, codebase-first, sharpens vocabulary against `CONTEXT.md`, captures terms via `ad-domain` and decisions via `ad-adr` (three-criteria rule). Upstream of `ad-ground`.
---

<background_information>
Implements ADR-0022 (`doc/adr/0022-agentic-grill-skill.md`) — the upstream-of-research phase. Process scaffold for sharpening fuzzy asks before any code, research, or spec work begins. Sits upstream of `ad-ground`; routes to it (and the other implementation-phase skills) when the question is sharp enough to act on.

No primary file output. Side-effects land in `CONTEXT.md` (via `ad-domain`) and ADRs (via `ad-adr`) — both lazy, both belonging to other skills.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions grilling, interview, fuzzy ask, or "stress test the plan", invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Grill is for the *fuzzy-question, scope-uncertain* regime. Run only when at least one holds:
- The user's ask uses vague or overloaded vocabulary ("account", "user", "thing", "stuff").
- The ask bundles ≥2 decisions into one question.
- The user has not stated the success condition or the failure mode.
- The same noun is used inconsistently across the request and the codebase.

Route elsewhere when:
- The question is sharp and research-ready → `ad-ground` (WORKFLOW §4 + §5).
- The technique is known and the ask is implementation-strategy choice → `ad-tdg` (WORKFLOW §9).
- The technique itself is uncertain across multiple plausible approaches → `ad-spike` (WORKFLOW §14).
- The ask is "this is broken" / "fix this bug" → `ad-diagnose` (WORKFLOW §15).

A well-scoped routine ask ("rename `foo` to `bar` everywhere") does not need grilling — just do the work.

Step 1 — codebase-first. Before asking a single question, look. Most "what does this do?" questions are answered by the code.

Process:
1. Read `CONTEXT.md` if it exists. Anchor vocabulary first; nothing else makes sense without it.
2. Read `CONTEXT-MAP.md` if it exists; load the per-context glossaries that match the surface.
3. `Glob` / `Grep` the surface the question touches — file names, function names, the user's nouns and their plausible aliases.
4. Read the matched files in the order most likely to answer the surface question.

Only after the codebase pass produces no answer does the skill ask the user.

Step 2 — one question at a time. Each question:
- Stands alone. Self-contained, no "and also...". Never a numbered list of three.
- Carries a recommended answer. The user can confirm with one word.
- Walks the design tree. Resolve the parent decision before its children.
- Waits for feedback. No proceeding past an unanswered question, no parallel branches.

Format:

```
[Branch: data model]
Q1: Should an Order own its Line Items, or should Line Items reference an Order by id?
Recommendation: Order owns Line Items. Locality wins — partial cancellation is the only operation that crosses the boundary, and it stays inside the aggregate.
```

After the user answers, the next question follows from the answer (depth-first), not from a pre-planned list.

Step 3 — challenge, sharpen, scenario-test. Three discipline patterns; mix as the conversation needs.

Challenge against the glossary. When the user's term conflicts with an entry in `CONTEXT.md`, surface the conflict immediately:

> "Your glossary defines *Cancellation* as full-order rollback, but you said partial cancellation is in scope — which is it? If both, are they the same domain concept or two distinct events?"

Sharpen fuzzy language. When the user uses a vague or overloaded term, propose the canonical resolution:

> "You're saying 'account' — do you mean *Customer* or *User*? Those are different things in this codebase: `Customer` carries billing, `User` carries auth. Which one drives this requirement?"

Scenario-test relationships. When two concepts interact, invent a concrete edge case that forces precision:

> "Concrete scenario: a Customer cancels Order #42 while the warehouse is mid-pick on Line Item 3. Who decides whether the in-flight pick continues?"

The scenarios are fabricated, not historical. Their job is to expose the boundary.

Step 4 — capture inline. When a term resolves or a decision crystallizes, capture immediately. Never batch.

Term resolved → `ad-domain`. Route to `ad-domain` with the canonical noun, the aliases to avoid, and the one-sentence definition. The skill writes to `CONTEXT.md`.

Decision crystallized → maybe `ad-adr`. Offer an ADR only when **all three** are true:

1. **Hard to reverse.** Changing the decision later costs meaningfully.
2. **Surprising without context.** A future reader will wonder *why was it done this way?*
3. **Result of a real trade-off.** There were named alternatives and the decision picked one for specific reasons.

If any of the three is missing, skip the ADR — the resolution lives in the conversation or the task `Notes` log.

Inline-capture rule. Capture as the resolution lands, not at the end of the session. Batched capture loses fidelity.
</instructions>

<output_contract>
Structured conversation. No primary file written. Side-effects:
- `CONTEXT.md` updates land via `ad-domain`.
- ADR drafts land via `ad-adr` when the three-criteria test passes.
- The grilling itself is the artifact — it lives in the chat transcript and (when one exists) the calling task's `Notes` log under a dated entry.

Each turn:
1. Codebase-first read (Step 1) — silent unless something interesting surfaces.
2. One question with recommendation (Step 2).
3. After user answer: capture if applicable (Step 4), then advance to the next branch (Step 2 again) until the design tree is resolved or the user routes out.
</output_contract>

## Next

- When the question is sharp and research-ready: `/ad-ground` (WORKFLOW §4 + §5).
- When the technique is known and the ask is implementation-strategy choice: `/ad-tdg` (WORKFLOW §9).
- When the technique itself is uncertain: `/ad-spike` (WORKFLOW §14).
- When the ask turned out to be "fix this bug": `/ad-diagnose` (WORKFLOW §15).
- After capture: stay in `/ad-grill` to walk the next branch of the design tree, or route out per above.
- If the session spans multiple working days: `/ad-task` to capture the open branches as a tracked task.
