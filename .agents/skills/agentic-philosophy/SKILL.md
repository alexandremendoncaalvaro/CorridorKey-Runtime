---
name: agentic-philosophy
description: Universal agent behavior — think before coding, ground in real patterns, prefer simplicity, make surgical changes, define verifiable goals, verify before claiming done. Auto-invokes on non-trivial changes, refactors, debugging, "think before coding", "ground before coding", "verify done", "before implementing", or whenever the task is ambiguous enough that guardrails matter.
---

<background_information>
Six behaviors apply to every non-trivial change. Bias toward caution over speed; for trivial diffs, use judgment.
</background_information>

<instructions>
**Think Before Coding.** Don't assume. Don't hide confusion. Surface tradeoffs. State assumptions explicitly; ask when uncertain. If multiple interpretations exist, present them — don't pick silently. If a simpler approach exists, say so. If something is unclear, stop, name the confusion, ask.

**Ground Before Coding.** Anchor in real patterns. Find the canonical/idiomatic way; note where you deviate and why. Find an existing example in the codebase and reuse its structure. Cite specific files, not "the codebase". Fetch via tools — don't dump code into context. For non-trivial changes, explore → plan → implement → commit. Skip for diffs describable in one sentence.

**Simplicity First.** Minimum code that solves the problem. No features beyond what was asked. No abstractions for single-use code. No "flexibility" or "configurability" that wasn't requested. No error handling for impossible scenarios. Comments justify why, not what. No commented-out code; no orphan `TODO`/`FIXME` without an issue/ADR/follow-up reference. If 200 lines could be 50, rewrite.

**Surgical Changes.** Touch only what you must. Don't "improve" adjacent code, comments, or formatting. Don't refactor things that aren't broken. Match existing style. If you notice unrelated dead code, mention it — don't delete it. Remove imports/variables/functions that YOUR changes made unused. Don't remove pre-existing dead code unless asked. Every changed line should trace directly to the user's request.

**Goal-Driven Execution.** Define success criteria. Loop until verified. Transform vague tasks into verifiable goals ("Add validation" → "Write tests for invalid inputs, then make them pass"). Before modifying a file, list which tests cover it; run, modify, run; if none, write one first. For multi-step tasks, state a brief plan with a verify step per item.

**Verify Before Claiming Done.** Type-check and tests verify code, not feature. For UI/runtime changes, exercise in a browser. Can't verify? Say so — don't claim success. Never bypass gates (`--no-verify`, skipped hooks, deleted failing tests).
</instructions>

<output_contract>
This skill emits no file. Its job is to set the agent's working posture for the next non-trivial change.
</output_contract>
