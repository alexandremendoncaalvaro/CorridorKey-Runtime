---
name: ad-diagnose
description: Disciplined diagnosis loop for hard bugs and performance regressions per WORKFLOW §15. Five phases — build a feedback loop, reproduce, hypothesise (3-5 ranked falsifiable), instrument, fix + regression-test. The feedback loop is the skill; everything else is mechanical. Triggers on "diagnose this", "debug this", "this is broken", "this is throwing", "performance regression", "find the bug", "build a repro", "feedback loop", "ranked hypotheses", "falsifiable", "/ad-diagnose". Routes to `ad-spike` when the technique is uncertain across approaches, `ad-grill` when the spec is unclear, `ad-tdg` when the bug is a clean ground-truth-pair regression.
summary: Disciplined diagnosis loop for hard bugs and performance regressions per WORKFLOW §15. Five phases — build a feedback loop (the skill itself), reproduce, hypothesise (3-5 ranked falsifiable), instrument (one variable at a time), fix + regression-test.
---

<background_information>
Implements WORKFLOW.md §15 per ADR-0021 (`doc/adr/0021-diagnose-discipline.md`). Disciplined diagnosis for hard bugs and performance regressions. Process scaffold; the output is the verified fix + regression test landing through normal commits.

The shape is grounded in Kernighan & Pike, *The Practice of Programming* (1999, ch. 5–6) and Karl Popper's falsifiability framing. The Phase-1 framing ("the loop is the skill — everything else is mechanical") is borrowed from `mattpocock/skills` with attribution.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions diagnosing, debugging, a performance regression, or building a repro, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Diagnose is for *hard bugs and performance regressions where the cause is unclear*. Run when at least one holds:
- A test is failing and the cause is not obvious from the diff.
- A symptom is reproducible but the chain of causes is not.
- A performance regression appeared between two known-good states.
- The user says "this is broken" / "this is throwing" / "this is slow" without a localized cause.

Route elsewhere when:
- The bug is one-line obvious (typo, off-by-one) — fix it directly.
- The bug is a clean ground-truth-pair regression (test was passing, output unchanged, now failing) → `ad-tdg` (WORKFLOW §9).
- The technique itself is uncertain across multiple plausible approaches → `ad-spike` (WORKFLOW §14).
- The spec or expected behavior is unclear → `ad-grill`.

Phase 1 — build a feedback loop. **This is the skill. Everything else is mechanical.** A fast, deterministic, agent-runnable pass/fail signal for the bug is what enables every later phase. Without a loop, no amount of staring at code finds the cause.

Spend disproportionate effort here. Be aggressive. Refuse to give up.

Loop construction (try in roughly this order):
1. Failing test at the seam closest to the bug — unit, integration, or e2e.
2. Curl / HTTP script against a running dev server.
3. CLI invocation with a fixture input, diffing stdout against a known-good snapshot.
4. Headless browser script (Playwright / Puppeteer) — drives the UI, asserts on DOM/console/network.
5. Replay a captured trace — save a real network request / payload / event log, replay it through the code path in isolation.
6. Throwaway harness — minimal subset of the system (one service, mocked deps) exercising the bug code path in a single function call.
7. Property / fuzz loop — for "sometimes wrong output", run 1000 random inputs and look for the failure mode.
8. Bisection harness — if the bug appeared between two known states, automate "boot at state X, check, repeat" so `git bisect run` can drive it.
9. Differential loop — same input through old vs new (or two configs), diff outputs.
10. HITL bash script — last resort. If a human must click, drive *them* with a structured loop so the signal still flows back.

Iterate on the loop itself. Once you have *a* loop:
- Faster? Cache setup, skip unrelated init, narrow the test scope.
- Sharper signal? Assert on the specific symptom, not "didn't crash".
- More deterministic? Pin time, seed RNG, isolate filesystem, freeze network.

A 30-second flaky loop is barely better than no loop. A 2-second deterministic loop is a debugging superpower.

Non-deterministic bugs. Goal is not a clean repro but a higher reproduction rate. Loop the trigger 100×, parallelize, add stress, narrow timing windows, inject sleeps. A 50%-flake bug is debuggable; 1% is not — keep raising the rate.

When you genuinely cannot build a loop. Stop and say so explicitly. List what you tried. Ask the user for one of: (a) access to whatever environment reproduces it, (b) a captured artifact (HAR file, log dump, core dump, screen recording with timestamps), (c) permission to add temporary production instrumentation. Do not proceed to Phase 3 without a loop.

Phase 2 — reproduce. Run the loop. Watch the bug appear.

Confirm:
- The loop produces the failure mode the user described — not a different failure that happens to be nearby. Wrong bug = wrong fix.
- The failure is reproducible across multiple runs (or, for non-deterministic bugs, at a high enough rate to debug).
- You captured the exact symptom (error message, wrong output, slow timing) so later phases can verify the fix actually addresses it.

Do not proceed until the bug reproduces.

Phase 3 — hypothesise. Generate **3–5 ranked hypotheses** before testing any of them. Single-hypothesis generation anchors on the first plausible idea — this is the most common debugging failure mode after no-loop.

Each hypothesis must be falsifiable — state the prediction it makes:

> Format: "If <X> is the cause, then <changing Y> will make the bug disappear / <changing Z> will make it worse."

If you cannot state the prediction, the hypothesis is a vibe — discard or sharpen.

Show the ranked list to the user before testing. They often have domain knowledge that re-ranks instantly ("we just deployed a change to #3"), or know hypotheses they have already ruled out. Cheap checkpoint, big time saver. Don't block on it — proceed with your ranking if the user is AFK.

Read `CONTEXT.md` if it exists and ADRs in `doc/adr/` covering the surface — domain vocabulary and prior decisions sharpen the hypotheses.

Phase 4 — instrument. Each probe maps to a specific prediction from Phase 3. Change one variable at a time.

Tool preference:
1. Debugger / REPL inspection if the env supports it. One breakpoint beats ten logs.
2. Targeted logs at the boundaries that distinguish hypotheses.
3. Never "log everything and grep".

Tag every debug log with a unique prefix, e.g. `[DEBUG-a4f2]`. Cleanup at the end becomes a single `grep`. Untagged logs survive; tagged logs die.

Perf branch. For performance regressions, logs are usually wrong. Establish a baseline measurement (timing harness, `performance.now()`, profiler, query plan), then bisect. Measure first, fix second.

If a probe falsifies its hypothesis, cross it off and move to the next-ranked one. Do not adapt the hypothesis to fit the probe.

Phase 5 — fix + regression-test. Write the regression test before the fix — but only if there is a correct seam for it.

A correct seam is one where the test exercises the real bug pattern as it occurs at the call site. If the only available seam is too shallow, a regression test there gives false confidence.

If no correct seam exists, that itself is the finding.

To pick the right routing branch below, determine the current profile: read `.agents/agentic-state.json` (or `.claude/agentic-state.json` for Claude Code installs) and inspect the `profile` field. Default to `team` if the file is absent.

- If the project profile is `team` or `mature`: hand off to `ad-deepen` (ADR-0020) with the specifics — the "test surface impact" line in the candidate template was made for this case.
- If the project profile is `poc` or `solo`: `ad-deepen` is not installed (premature per ADR-0020 §4). Capture the seam gap in the commit message body and the task `Notes` log as a finding for a future deepening pass; if the project is graduating to `team`, re-init at the higher profile to enable `ad-deepen` then.

If a correct seam exists:
1. Turn the minimised repro into a failing test at that seam.
2. Watch it fail.
3. Apply the fix.
4. Watch it pass.
5. Re-run the Phase 1 feedback loop against the original (un-minimised) scenario.

Cleanup before declaring done:
- Original repro no longer reproduces (re-run Phase 1 loop).
- Regression test passes (or absence of correct seam is documented as a finding for `ad-deepen`).
- All `[DEBUG-...]` instrumentation removed (`grep` the prefix to verify zero hits).
- Throwaway prototypes deleted (or moved to a clearly-marked debug location).
- The hypothesis that turned out correct is stated in the commit / PR message — so the next debugger learns.
</instructions>

<output_contract>
No primary file written. The output is the verified fix + regression test that lands through normal commits. Loop construction notes, ranked hypotheses, falsified ones, and the winning hypothesis go into the commit message body or the task's `Notes` log when one exists.

Each session produces:
1. The loop (Phase 1) — described in the commit message; a failing test if one was added.
2. The captured symptom (Phase 2) — quoted in the commit message.
3. The ranked hypothesis list (Phase 3) — listed in the commit message body with the winner marked.
4. The fix (Phase 5).
5. The regression test (Phase 5) — or the explicit finding that no correct seam exists.
</output_contract>

## Next

- After fix + regression test land: `/ad-review main..HEAD` (or current scope) before merge — WORKFLOW §10.
- If Phase 5 found no correct seam for the regression test and the profile is `team` / `mature`: `/ad-deepen` to surface the deepening opportunity that would create one. On `poc` / `solo`, capture the gap in the commit message and the task Notes — `ad-deepen` is not installed at those profiles.
- If Phase 1 could not build a loop and the user provided a captured artifact: re-enter Phase 1 with the artifact as the loop seed.
- If the bug turned out to be a vocabulary drift: `/ad-domain` to update `CONTEXT.md` and `/ad-audit` to scan for further drift.
- If the bug is one of several in a related cluster: `/ad-task` to capture the cluster as a tracked task with this diagnose run cited in the Notes.
