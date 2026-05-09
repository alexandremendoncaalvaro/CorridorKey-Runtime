---
name: agentic-philosophy
description: Universal agent behavior — think before coding, ground in real patterns, prefer simplicity, make surgical changes, define verifiable goals, verify before claiming done. Auto-invokes on non-trivial changes, refactors, debugging, "think before coding", "ground before coding", "verify done", "before implementing", or whenever the task is ambiguous enough that guardrails matter.
---

# /agentic-philosophy

Six behaviors apply to every non-trivial change. Bias toward caution over speed; for trivial diffs, use judgment.

## Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:

- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## Ground Before Coding

**Anchor in real patterns. Research the canonical path.**

- Find the canonical/idiomatic way to do it. Note where you deviate and why.
- Find an existing example in the codebase; reuse its structure.
- Cite specific files, not "the codebase". Fetch via tools — don't dump code into context.
- For non-trivial changes, explore (read-only) → plan → implement → commit. Skip for diffs you can describe in one sentence.

## Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- Comments justify *why* a non-obvious choice was made, not *what* the line does. No commented-out code; no orphan `TODO`/`FIXME` — every deferred item references an issue, ADR, or follow-up.
- If you write 200 lines and it could be 50, rewrite it.

Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:

- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.

When your changes create orphans:

- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

## Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:

- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

Before modifying a file, list which tests cover it. Run. Modify. Run. If none, write one first.

For multi-step tasks, state a brief plan:

```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

## Verify Before Claiming Done

- Type-check and tests verify *code*, not *feature*.
- For UI/runtime changes, exercise the feature in a browser.
- Can't verify it? Say so. Don't claim success.
- Never bypass gates (`--no-verify`, skipped hooks, deleted failing tests).
