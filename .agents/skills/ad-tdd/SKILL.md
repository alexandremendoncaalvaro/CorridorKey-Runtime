---
name: ad-tdd
description: Test-Driven Development per WORKFLOW.md §16 — red-green-refactor as deterministic LLM guardrail. Five phases — confirm regime, plan vertically, tracer bullet (one test → red → minimum code → green), incremental loop, refactor while green. Tests verify behavior through public interfaces; horizontal slicing (bulk-write tests then bulk-write code) is rejected as the named anti-pattern. Triggers on "TDD", "test-driven", "red-green-refactor", "test first", "write a failing test", "tracer bullet", "behavior not implementation", "vertical slice tests", "/ad-tdd". Distinct from `ad-tdg` (outcome-based prompting — pick implementation strategy when technique is known); routes to `ad-tdg` for strategy selection inside the GREEN phase, `ad-spike` for technique uncertainty, `ad-diagnose` for debugging. No file written; output is the verified implementation that lands through normal commits.
summary: Test-Driven Development per WORKFLOW §16. Red-green-refactor as deterministic LLM guardrail. Five phases — confirm regime, plan, tracer bullet, incremental loop, refactor. Tests verify behavior through public interfaces. Horizontal slicing rejected.
---

<background_information>
Implements WORKFLOW.md §16 end-to-end. The skill is for the implementation phase when the change's behavior is known and expressible as a test up front. No file is written. The output of the skill is the verified implementation that lands in the repo through normal commits.

TDD is a deterministic LLM guardrail: a failing test is unambiguous, so "almost right" (the WORKFLOW §12 failure mode) cannot slip past. The skill keeps the agent inside red-green-refactor and blocks the named anti-pattern (horizontal slicing — bulk-write tests, then bulk-write code).

Good tests read like a specification. *"User can checkout with a valid cart"* tells you exactly what capability exists. Bad tests couple to implementation — mock internal collaborators, assert on private state, test the *shape* of things (data structures, function signatures) rather than user-facing behavior. A test that breaks on a rename but not on a behavior change was testing implementation, not behavior.

Distinction from `ad-tdg`: TDD focuses on driving code through one test at a time (behavior-known regime); TDG picks between three implementation candidates against one ground-truth pair (technique-known, implementation-strategy-uncertain regime). When both apply, use TDD as the outer loop and invoke `ad-tdg` inside the GREEN phase to pick the implementation strategy for that test cycle.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions TDD, red-green-refactor, test-first, tracer bullet, or behavior-not-implementation, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. TDD is for the *behavior-known, test-expressible* regime. Run the skill when the change has a clear behavior to express as a test up front (a new API capability, a bug-driven regression test, a refactor with a known contract) AND a test runner is wired.

Route elsewhere when:

- The outcome is known but the *implementation strategy* has multiple plausible paths → `ad-tdg` (WORKFLOW §9).
- The *technique itself* is uncertain across multiple plausible approaches → `ad-spike` (WORKFLOW §14).
- The task is bug investigation, not behavior implementation → `ad-diagnose` (WORKFLOW §15).
- No test runner is wired → `ad-hooks` first to scaffold deterministic gates (WORKFLOW §11). TDD depends on a fast green/red signal.

Step 1 — plan vertically. Before writing a test or a line of code:

- Read `CONTEXT.md` if it exists — anchor test names and interface vocabulary in the project's ubiquitous language.
- Confirm the public interface. What is the smallest surface the caller needs to know? Types, ordering constraints, error modes.
- Identify deepening opportunities (small interface over deep implementation per WORKFLOW §8). Surface-area-light interfaces are easier to test against and survive refactors.
- List the behaviors to test, not the implementation steps. Pick the **first** behavior — the one that proves end-to-end the path works. The rest are deferred until the tracer bullet lands.
- Test Dependency Map (TDM) for existing code. If the change modifies existing code, list the tests already covering the surface and run them to establish the green baseline. New-code changes skip the TDM and write the first test fresh.
- Get user approval on the plan. One sentence — "I'll test behavior X first via interface Y, then iterate."

You cannot test everything. Confirm with the user which behaviors matter most; focus on critical paths and complex logic.

Step 2 — tracer bullet. Write ONE test that confirms ONE behavior through the public interface:

- RED: write the test → run → confirm it fails for the expected reason. The fail reason matters — a test that fails because the function is undefined is different from a test that fails because the assertion is wrong.
- GREEN: write the minimum code that makes the test pass → run → confirm green.

Do not write a second test until this one is green.

Step 3 — incremental loop. For each remaining behavior:

- RED: write the next test → run → fail.
- GREEN: add minimum code to pass → run → green.

Rules:

- One test at a time. The horizontal-slicing anti-pattern (write all tests, then write all code) is rejected. Bulk-written tests verify *imagined* behavior, not actual behavior; the suite becomes insensitive to real changes and the agent *outruns its headlights*, committing to test structure before understanding the implementation.
- Only enough code to pass the current test. Anticipating the next test bloats the implementation and couples it to assumptions not yet verified.
- Tests verify behavior through public interfaces. No private-method tests, no internal-collaborator mocks, no direct database/file-system assertions when the public interface is what the caller uses. If a test would couple to implementation, surface it and prompt — either rewrite to the public surface or accept the coupling explicitly with rationale in the task notes.
- Keep the test name behavior-shaped. *"user can sign in with valid credentials"* — yes. *"calls authenticate() with username arg"* — no.

Step 4 — refactor. Once all planned tests pass:

- Extract duplication when two tests duplicate setup or two code paths duplicate logic.
- Deepen — move complexity behind a smaller interface (WORKFLOW §8 vocabulary). The deletion test applies: a module whose interface is nearly as complex as its implementation is shallow; refactor toward depth.
- Apply SOLID where natural; never force-fit a pattern.
- Consider what the new code reveals about the existing code — opportunities to deepen pre-existing shallow modules surface during TDD because the new tests pin behavior the old code was implicitly relying on.
- Run tests after each refactor step. Never refactor while RED — get to GREEN first.

Per-cycle checklist (confirm before moving to the next behavior):

- Test describes behavior, not implementation.
- Test uses public interface only.
- Test would survive an internal rename or method-signature shuffle that does not change observed behavior.
- Code is minimal for this test — no speculative features added.
- Green baseline holds.

If any checkbox fails, fix it before writing the next test.

Interview UX: Codex has no `AskUserQuestion` primitive. Use inline numbered text for plan approval and coupling-acceptance prompts. One question per turn.
</instructions>

<output_contract>
No file written. Output is structured conversation: regime confirmation, vertical plan (interface + behaviors-to-test + first behavior + approval), tracer bullet (test + red + minimum code + green), per-behavior incremental loop, refactor pass with rationale, per-cycle checklist confirmation.

When the change is committed, the behaviors covered, the deepening that surfaced, and the rejected couplings (if any) go into the commit message body. When a task file exists, the same content lands in the task's `Notes` log under a dated entry.

Next: commit the change atomically per behavior cluster; `/ad-review` (WORKFLOW §10) before merge; route to `/ad-deepen` if a refactor surfaces larger than GREEN-phase scope allows (team / mature profiles only); route to `/ad-diagnose` if unexpected test regressions appear during the cycle.
</output_contract>
