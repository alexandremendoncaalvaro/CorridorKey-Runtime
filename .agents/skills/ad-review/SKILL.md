---
name: ad-review
description: |
  Run this skill whenever the user asks for a code review, diff review, branch review, PR review, "revisa esse diff", "faz code review", "review main..HEAD", or invokes /ad-review.
  Mechanical shape: this session writes TWO handoff `.md` files to `.agentic/reviews/` (one Standards-axis, one Spec-axis) then STOPS. The user then runs `/clear` + paste twice — once per axis — in fresh Codex sessions, and collates the two reports by hand. NO sub-agents are spawned (Codex has no subagent primitive); NO Task tool is called; the current session does not produce findings. The two-axis split — Standards (does the diff conform to AGENTS.md / ARCHITECTURE.md / GUIDELINES.md / CONTEXT.md / accepted ADRs?) and Spec (does the diff match the originating task / spec / PRD?) — exists so neither axis can mask the other.
summary: Writes 2 fresh-context handoff `.md` files (Standards-axis + Spec-axis) and stops. User runs `/clear` + paste twice in fresh sessions. No subagent primitive on Codex — handoff files replace it. Adapted from mattpocock/skills `review` per WORKFLOW §10.
---

<how-this-runs-on-codex>
Codex has no subagent primitive. The skill cannot spawn parallel reviewers; the user runs them manually as two sequential `/clear` passes.

Mechanical shape — do not deviate:

```
THIS SESSION:
  1. Scope the review (which range / PR / commit?).
  2. Identify Standards sources (AGENTS.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md, accepted ADRs).
  3. Identify Spec source (task Acceptance Criteria → spec → PRD → issue body).
  4. Write TWO handoff files to .agentic/reviews/:
       <ISO>-<scope>-standards.md
       <ISO>-<scope>-spec.md
  5. Print the two file paths + paste-and-/clear instructions.
  6. STOP. Do not review the diff yourself in this session.

THE USER THEN:
  7. /clear → paste contents of *-standards.md → send. Capture the report.
  8. /clear → paste contents of *-spec.md     → send. Capture the report.
  9. Collate the two reports. Standards Blockers first; Spec Blockers second.
```

If the Spec source resolves to nothing, the Spec handoff is a single-line stub (`no spec source provided — skip this axis`) and the user runs only step 7.
</how-this-runs-on-codex>

<anti-patterns>
- Do NOT call `Task`. Codex has no Task tool routing to subagents — the call will fail or hallucinate.
- Do NOT spawn a "fresh-context-reviewer" agent. That subagent is a Claude Code primitive; on Codex the handoff files replace it.
- Do NOT review the diff in this session. The whole point of the skill is to push the review into a fresh context — reviewing inline defeats §10.
- Do NOT merge the two axes into one combined handoff. The split is the rigor; merging is the bias the skill exists to prevent.
- Do NOT skip Step 4's write-to-disk and inline the handoff body instead. The user needs files they can `cat | pbcopy`.
</anti-patterns>

<background_information>
Implements WORKFLOW §10 (Reviewer With Fresh Context). The current session is biased about the code it produced — the same reasoning that wrote it defends it. This skill assembles two clean handoffs (Standards-axis, Spec-axis); the user re-loads each into a fresh session via `/clear` + paste, then collates the two reports.

The two-axis split is borrowed from [mattpocock/skills/review](https://github.com/mattpocock/skills/blob/main/skills/in-progress/review/SKILL.md) and bound to this kit's six-layer artifact stack. A Spec pass can mask a Standards fail (and vice versa); reporting the axes separately prevents that.
</background_information>

<instructions>
Step 0 — announce. Before doing anything, print one line so the user knows what's about to happen:

```
Running ad-review. I will write 2 handoff files to .agentic/reviews/ and stop. You then /clear + paste each in a fresh Codex session. I will NOT review the diff in this session.
```

Step 1 — scope the review. Confirm what to review. Default scopes, in priority order:
1. User-named ref or PR (`ad-review main..HEAD`, `ad-review <commit-sha>`).
2. Current branch vs `main` (`git diff main...HEAD`).
3. Working-tree changes (`git diff` plus `git diff --staged`).

If no diff exists, stop and tell the user — there's nothing to review.

Capture the diff command once: `git diff <range>` (use `...` three-dot for ref-vs-ref so the comparison is against the merge-base). Note the commit list with `git log <range> --format=%B`.

Step 2 — identify Standards sources. Read what exists; do not fabricate references.
- `AGENTS.md` at the repo root.
- `ARCHITECTURE.md` at the repo root.
- `GUIDELINES.md` at the repo root.
- `CONTEXT.md` at the repo root, or `CONTEXT-MAP.md` plus per-context `CONTEXT.md`s.
- Every ADR under `doc/adr/` with `Status: accepted` whose subject is touched by the diff. When in doubt, include rather than skip.
- `CONTRIBUTING.md` if present.
- Machine-enforced standards (`.editorconfig`, `eslint.config.*`, `biome.json`, `prettier.config.*`, `tsconfig.json`) — note their presence so the Standards handoff instructs the fresh-context reader to skip what tooling already enforces.

Step 3 — identify the Spec source. In this order, take the first that resolves:
1. Task references in the diff or recent commit messages (`Task NNNN`, `0NNN-`, `Closes task-0042`) → read the file's Acceptance Criteria and Plan sections.
2. An originating spec under `doc/specs/` whose filename matches the branch name or the dominant feature touched by the diff.
3. A parent PRD under `doc/product/` referenced by the spec.
4. Issue references in commit messages (`#123`, `Closes #45`) — fetch via `gh issue view` if available.

If nothing resolves, the Spec handoff becomes a single-line stub (`no spec source provided`) and the user skips the second `/clear` pass.

Step 4 — build the two axis-bounded handoffs. Each handoff receives only the slice for its axis. Do not paste the Spec slice into the Standards handoff or vice versa — that is the bias the split exists to prevent.

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

If Step 3 found no spec, write the Spec handoff as a single line: `no spec source provided — skip this axis`.

Step 5 — write both handoffs to disk. Save them at:
- `.agentic/reviews/<ISO-timestamp>-<scope-slug>-standards.md`
- `.agentic/reviews/<ISO-timestamp>-<scope-slug>-spec.md`

`<scope-slug>` encodes the review target (`branch-vs-main`, `pr-42`, `commit-abc1234`, `working-tree`). Create the directory if missing. Advise the user to add `.agentic/reviews/` to `.gitignore` — handoffs are ephemeral.

If the combined diff spans >50 files, ask the user to narrow scope before continuing — running two passes on a giant diff is wasteful.

Step 6 — instruct the user, then STOP. After writing both handoffs, output exactly this and do not continue:

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

You MUST stop here. Do not proceed to review the diff in this session — the whole point of §10 is to push the review into a fresh context.
</instructions>

<output_contract>
Two handoff files at `.agentic/reviews/<ISO-timestamp>-<scope-slug>-standards.md` and `.agentic/reviews/<ISO-timestamp>-<scope-slug>-spec.md` (the latter may be a single-line stub when no spec source resolves), plus an instruction telling the user how to load each into a fresh Codex session via `/clear` and paste. The current session does NOT produce findings — the two fresh sessions do, after the user runs them. This honors WORKFLOW §10 by enforcing context isolation through `/clear` (Codex has no subagent primitive) and preserves the two-axis split that prevents one axis from masking the other.
</output_contract>

## Next

- Address every Standards Blocker before merge — that's the code-quality hard gate. Re-run `ad-review` on the fix to confirm it cleared.
- Address Spec Blockers next — implementation-vs-spec drift is the second hard gate.
- Each Concern (from either axis) becomes a follow-up `ad-task`; do not let them silently accumulate.
- Notes are informational; close them out in the original task's `Notes` log if relevant.
- If the Spec axis was skipped, decide whether an `ad-spec` is overdue — work without a spec means future reviews are Standards-only.
- Once both axes are clear: merge per project conventions.
