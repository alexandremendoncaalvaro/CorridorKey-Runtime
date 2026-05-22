---
name: ad-review
description: Two-axis fresh-context code review per WORKFLOW §10. Splits the review into Standards (does the diff conform to AGENTS.md / ARCHITECTURE.md / GUIDELINES.md / CONTEXT.md / accepted ADRs?) and Spec (does the diff match the originating task / spec / PRD?), writes two axis-bounded handoffs so the user runs them as two sequential `/clear` passes. The split prevents one axis from masking the other. Adversarial framing — never emits an "approve" verdict.
summary: Two-axis fresh-context code review per WORKFLOW §10 — Standards (binding docs) and Spec (originating task / spec / PRD) run as two sequential `/clear` handoffs on Codex (no subagent primitive). Adapted from mattpocock/skills `review` and bound to the kit's six-layer artifact stack.
---

<background_information>
Implements WORKFLOW §10 (Reviewer With Fresh Context). The current session is biased about the code it produced — the same reasoning that wrote it defends it. This skill assembles two clean handoffs (Standards-axis, Spec-axis); the user re-loads each into a fresh session via `/clear` + paste, then collates the two reports.

The two axes are deliberately separate so a Spec pass cannot mask a Standards fail (and vice versa) — the dichotomy is borrowed from [mattpocock/skills/review](https://github.com/mattpocock/skills/blob/main/skills/in-progress/review/SKILL.md) and bound to this kit's six-layer artifact stack.

Codex has no native subagent primitive. Parity with Claude Code's two parallel Task calls is preserved through two sequential manual handoffs — slightly more friction, same rigor.
</background_information>

<instructions>
Step 0 — scope the review. Confirm what to review. Default scopes, in priority order:
1. User-named ref or PR (`ad-review main..HEAD`, `ad-review <commit-sha>`).
2. Current branch vs `main` (`git diff main...HEAD`).
3. Working-tree changes (`git diff` plus `git diff --staged`).

If no diff exists, stop and tell the user — there's nothing to review.

Capture the diff command once: `git diff <range>` (use `...` three-dot for ref-vs-ref so the comparison is against the merge-base). Note the commit list with `git log <range> --format=%B`.

Step 1 — identify Standards sources. Read what exists; do not fabricate references.
- `AGENTS.md` at the repo root.
- `ARCHITECTURE.md` at the repo root.
- `GUIDELINES.md` at the repo root.
- `CONTEXT.md` at the repo root, or `CONTEXT-MAP.md` plus per-context `CONTEXT.md`s.
- Every ADR under `doc/adr/` with `Status: accepted` whose subject is touched by the diff. When in doubt, include rather than skip.
- `CONTRIBUTING.md` if present.
- Machine-enforced standards (`.editorconfig`, `eslint.config.*`, `biome.json`, `prettier.config.*`, `tsconfig.json`) — note their presence so the Standards handoff instructs the reviewer to skip what tooling already enforces.

Step 2 — identify the Spec source. In this order, take the first that resolves:
1. Task references in the diff or recent commit messages (`Task NNNN`, `0NNN-`, `Closes task-0042`) → read the file's Acceptance Criteria and Plan sections.
2. An originating spec under `doc/specs/` whose filename matches the branch name or the dominant feature touched by the diff.
3. A parent PRD under `doc/product/` referenced by the spec.
4. Issue references in commit messages (`#123`, `Closes #45`) — fetch via `gh issue view` if available.

If nothing resolves, the Spec handoff becomes a single-line stub (`no spec source provided`) and the user skips the second `/clear` pass.

Step 3 — build two axis-bounded handoffs. Each handoff receives only the slice for its axis. Do not paste the Spec slice into the Standards handoff or vice versa — that is the bias the split exists to prevent.

Standards handoff body:

```
=== AGENTIC-REVIEW HANDOFF — STANDARDS AXIS ===

You are a senior engineer reviewing a junior PR with no prior context. The
diff and the Standards sources below are the only evidence. Do not infer
history or trust the author's intent.

Axis: Standards. Report only:
- Bugs (null/undefined paths, off-by-one, race conditions, broken invariants).
- Coupling (modules that shouldn't know about each other, leaked abstractions).
- Edge cases (empty inputs, large inputs, concurrent access, permission errors).
- Diff violations of any standards source below.
Skip what tooling already enforces (lint, format, type-check) — listed under
TOOLING NOTE. Skip Spec-axis findings (missing requirements, scope creep) —
a separate handoff covers those.

Output: group findings by severity (Blocker / Concern / Note). Each finding
one line: `file:line: <severity>: <problem>. <fix>.` Severity is the literal
word `Blocker`, `Concern`, or `Note` — no emoji.

End with: `Standards: ship as-is`, `Standards: ship with the Concerns logged`,
or `Standards: don't ship until Blockers resolved`. Do NOT synthesize an
overall approval — that crosses axes.

--- DIFF ---
<git diff output>

--- STANDARDS SOURCES ---
<AGENTS.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md / CONTEXT-MAP.md,
applicable accepted ADRs, CONTRIBUTING.md>

--- TOOLING NOTE ---
<list of machine-enforced configs found — skip what they already check>

=== END HANDOFF ===
```

Spec handoff body:

```
=== AGENTIC-REVIEW HANDOFF — SPEC AXIS ===

You are a senior engineer reviewing a junior PR with no prior context. The
diff and the Spec sources below are the only evidence. Do not infer history
or trust the author's intent.

Axis: Spec. Report only:
  (a) requirements the spec asked for that are missing or partial;
  (b) behaviour in the diff that wasn't asked for (scope creep);
  (c) requirements that look implemented but where the implementation looks
      wrong against the spec line.
Quote the spec line for each finding. Skip Standards-axis findings (bugs,
coupling, edge cases) — a separate handoff covers those.

Output: group findings by severity (Blocker / Concern / Note). Each finding
one line: `file:line: <severity>: <problem> (spec: <quoted-line>). <fix>.`
Severity is the literal word `Blocker`, `Concern`, or `Note` — no emoji.

End with: `Spec: ship as-is`, `Spec: ship with the Concerns logged`, or
`Spec: don't ship until Blockers resolved`. Do NOT synthesize an overall
approval — that crosses axes.

--- DIFF ---
<git diff output>

--- SPEC SOURCES ---
<task file Acceptance Criteria + Plan, originating spec, parent PRD,
recent commit messages, originating issue body if fetched>

=== END HANDOFF ===
```

If Step 2 found no spec, write the Spec handoff as a single line: `no spec source provided — skip this axis`.

Step 4 — write both handoffs to disk. Save them at:
- `.agentic/reviews/<ISO-timestamp>-<scope-slug>-standards.md`
- `.agentic/reviews/<ISO-timestamp>-<scope-slug>-spec.md`

`<scope-slug>` encodes the review target (`branch-vs-main`, `pr-42`, `commit-abc1234`, `working-tree`). Create the directory if missing. Advise the user to add `.agentic/reviews/` to `.gitignore` — handoffs are ephemeral.

If the combined diff spans >50 files, ask the user to narrow scope before continuing — running two passes on a giant diff is wasteful.

Step 5 — instruct the user. After writing both handoffs, output exactly this and stop:

```
Handoffs saved:
  Standards: <standards-path>
  Spec:      <spec-path>   (or "skipped — no spec source provided")

Run the Standards pass first:

  cat <standards-path> | pbcopy        # macOS
  xclip -selection clipboard < <standards-path>   # Linux

In Codex: `/clear`, paste, send. Capture the report.

Then run the Spec pass (if not skipped):

  cat <spec-path> | pbcopy
  xclip -selection clipboard < <spec-path>

In Codex: `/clear`, paste, send. Capture the report.

Collate the two reports yourself — they are deliberately separate so neither
axis masks the other. Address Standards Blockers first (code-quality hard
gate), then Spec Blockers (implementation-vs-spec drift).
```

Do not proceed past this point in the current session.
</instructions>

<output_contract>
Two handoff files at `.agentic/reviews/<ISO-timestamp>-<scope-slug>-standards.md` and `.agentic/reviews/<ISO-timestamp>-<scope-slug>-spec.md` (the latter may be a single-line stub when no spec source resolves), plus an instruction telling the user how to load each into a fresh Codex session via `/clear` and paste. The current session does not produce findings — the two fresh sessions do, after the user runs them. This honors WORKFLOW §10 by enforcing context isolation through `/clear` (Codex has no subagent primitive) and preserves the two-axis split that prevents one axis from masking the other.
</output_contract>

## Next

- Address every Standards Blocker before merge — that's the code-quality hard gate. Re-run `ad-review` on the fix to confirm it cleared.
- Address Spec Blockers next — implementation-vs-spec drift is the second hard gate.
- Each Concern (from either axis) becomes a follow-up `ad-task`; do not let them silently accumulate.
- Notes are informational; close them out in the original task's `Notes` log if relevant.
- If the Spec axis was skipped, decide whether an `ad-spec` is overdue — work without a spec means future reviews are Standards-only.
- Once both axes are clear: merge per project conventions.
