---
name: ad-review
description: |
  Run this skill when the user explicitly invokes `/ad-review` or names it ("run ad-review", "use the ad-review skill"), or when the user asks for a code review with an explicit scope ("review this branch", "review main..HEAD", "revisa esse diff <range>"). Auto-trigger note: `allow_implicit_invocation: true` is set so review-language can fire the skill, but this also means broad review-adjacent conversation may auto-invoke a multi-step file-writing workflow. If a request is ambiguous, ask the user to confirm scope before invoking.
  Mechanical shape: ONE pass in the current session. The skill assembles the diff plus the relevant context, then produces a single review with findings grouped under `## Standards Findings` and `## Spec Findings` — two axes, one session. Standards = does the diff conform to AGENTS.md / ARCHITECTURE.md / GUIDELINES.md / CONTEXT.md / accepted ADRs? Spec = does the diff match the originating task / spec / PRD? The two-axis structure exists so neither axis masks the other.
  No `/clear`. No silent subagent spawn from the skill: Codex subagents are explicit user-directed workflows. The skill writes a single audit-trail handoff file at `.agentic/reviews/<ISO>-<scope>.md` for the record, then performs the review inline. For §10 ideal, use the bundled `fresh-context-reviewer` Codex subagent against that audit-trail file.
summary: Two-axis code review per WORKFLOW §10. Claude Code uses fresh-context subagents; Codex writes an audit trail, reviews inline by default, and ships a reviewer subagent for explicit escalation.
---

<how-this-runs-on-codex>
Codex skills run inline in the current session. Codex supports subagent workflows, but spawning is explicit user-directed orchestration, not something this skill does silently. So the default review is one pass with disciplined axis-separated output; the optional escalation uses the bundled `fresh-context-reviewer` subagent against the persisted audit-trail file.

Mechanical shape:

```
THIS SESSION:
  1. Scope the review (which range / PR / commit?).
  2. Read Standards sources (AGENTS.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md, accepted ADRs).
  3. Read Spec source (task Acceptance Criteria → spec → PRD → issue body).
  4. Write the assembled context to .agentic/reviews/<ISO>-<scope>.md (audit trail).
  5. Perform the review in this session. Output findings under two headings:
        ## Standards Findings   (bugs, coupling, edge cases, doc violations)
        ## Spec Findings        (missing requirements, scope creep, wrong impl vs quoted spec line)
  6. End with a one-line aggregate (counts per axis + worst finding).
```

The two-axis split is structural rigor — same reviewer, but findings must be classified before mixing. A change that passes Spec can still fail Standards (and vice versa); reporting axes separately prevents one from masking the other.
</how-this-runs-on-codex>

<anti-patterns>
- Do NOT call `/clear` and ask the user to paste handoffs. `/clear` nukes the terminal + context together on Codex — the UX is heavy and unnecessary. Single-session review is the right shape on Codex.
- Do NOT silently spawn subagents from inside this skill. Codex subagents are explicit user-directed workflows; the audit-trail file is the context packet the user can hand to a spawned reviewer.
- Do NOT merge the two axes into a single findings list. The split is the rigor; merging is the bias the skill exists to prevent.
- Do NOT produce an "approve" verdict. WORKFLOW §10 frames the review as adversarial; approval is the senior engineer's call after weighing both axes.
- Do NOT skip writing the audit-trail file at `.agentic/reviews/`. The file lets the user re-run the review later against an updated diff or share it with a teammate.
- Do NOT begin Step 1 or any file I/O before printing the Step 0 announce line. The user must see the operational shape (one pass, one audit-trail file, axis-separated output) before the skill starts reading/writing — otherwise the agent silently drifts into work the user did not consent to.
- Do NOT skip Step 7 when a Standards-axis finding touches a binding doc (AGENTS / ARCHITECTURE / GUIDELINES / CONTEXT / ADR). Omitting the escalation recommendation lets a downgraded premise-critical finding ship silently.
- Do NOT write the handoff if `git rev-list --count <range>` and the count of `### <sha>` entries in `## Spec slice — commit messages` disagree. Downstream review fidelity depends on this invariant.
</anti-patterns>

<background_information>
Implements WORKFLOW §10 (Reviewer With Adversarial Discipline). On Claude Code, §10 is delivered via two parallel `Task` subagent calls with fresh context. On Codex, the skill defaults to **structural axis separation inside a single review pass** and ships a bundled `fresh-context-reviewer` TOML for explicit user-spawned escalation. The inline reviewer cannot rationalize a Spec pass as covering Standards (or vice versa) because the output schema forces both lists to be produced separately.

The two-axis dichotomy is borrowed from [mattpocock/skills/review](https://github.com/mattpocock/skills/blob/main/skills/in-progress/review/SKILL.md) and bound to this kit's six-layer artifact stack (Constitution → Domain → Product → Spec → Plan/Decisions → Code).

For Codex users who want true fresh-context review (the §10 ideal), spawn the bundled subagent manually after the skill writes the audit-trail file — see the "Optional escalation" block at the bottom of the instructions.
</background_information>

<instructions>
Step 0 — announce. Print the shape so the user sees it before any work:

```
Running ad-review (Codex single-pass two-axis). I will read the diff and binding context, write an audit trail to .agentic/reviews/, then report findings under ## Standards Findings and ## Spec Findings in this session.

NOTE on §10 fidelity: a single-session reviewer with both axes loaded can rationalize across them (axis-bleed). If any Standards finding I produce touches a binding doc (AGENTS / ARCHITECTURE / GUIDELINES / CONTEXT / ADR), I will recommend the user-initiated subagent escalation at Step 7 so you can re-run that finding under fresh context. The escalation TOML schema is at the bottom of this skill.
```

Step 1 — scope the review. Confirm what to review. Default scopes, in priority order:
1. User-named ref or PR (`ad-review main..HEAD`, `ad-review <commit-sha>`).
2. Current branch vs `main` (`git diff main...HEAD`).
3. Working-tree changes (`git diff` plus `git diff --staged`).

If no diff exists, stop and tell the user — there's nothing to review.

Capture the diff command once: `git diff <range>` (use `...` three-dot for ref-vs-ref so the comparison is against the merge-base). Note the commit list with `git log <range> --format=%B`.

**Handoff-integrity gate.** Compute the commit count: `git rev-list --count <range>`. Bind it to `N`. The handoff header must include the literal line `Range: <range> (N commits)`. The `## Spec slice — commit messages` section must contain exactly `N` `### <sha> <subject>` entries. If your body has fewer, you mis-bounded the range — stop and re-scope. The single-session review signal is bounded above by handoff fidelity; a quiet count mismatch produces a silently-incomplete review.

Size guard: if `git diff <range> --stat` reports >50 files, ask the user to narrow scope before continuing — reviewing a giant diff in one pass loses signal.

Step 2 — read Standards sources. Read what exists; do not fabricate references.
- `AGENTS.md` at the repo root.
- `ARCHITECTURE.md` at the repo root.
- `GUIDELINES.md` at the repo root.
- `CONTEXT.md` at the repo root, or `CONTEXT-MAP.md` plus per-context `CONTEXT.md`s.
- Every ADR under `doc/adr/` with `Status: accepted` whose subject is touched by the diff. When in doubt, include rather than skip.
- `CONTRIBUTING.md` if present.
- Machine-enforced configs (`.editorconfig`, `eslint.config.*`, `biome.json`, `prettier.config.*`, `tsconfig.json`) — note their presence and skip re-checking what tooling already enforces.

Step 3 — read Spec source. In this order, take the first that resolves:
1. Task references in the diff or recent commit messages (`Task NNNN`, `0NNN-`, `Closes task-0042`) → read the file's Acceptance Criteria and Plan sections.
2. An originating spec under `doc/specs/` whose filename matches the branch name or the dominant feature touched by the diff.
3. A parent PRD under `doc/product/` referenced by the spec.
4. Issue references in commit messages (`#123`, `Closes #45`) — fetch via `gh issue view` if available.

If nothing resolves, mark Spec as `no spec source provided`. The Spec axis output will say so explicitly and report no findings.

Step 4 — write the audit-trail handoff. Persist the assembled context at `.agentic/reviews/<ISO-timestamp>-<scope-slug>.md` (single file, not two). `<scope-slug>` encodes the review target (`branch-vs-main`, `pr-42`, `commit-abc1234`, `working-tree`). Create the directory if missing.

File body:

```
=== AGENTIC-REVIEW HANDOFF (single-session, two-axis) ===

Range: <range-spec> (N commits)        # N = git rev-list --count <range-spec>; assert MUST equal the count of ### entries below

--- DIFF ---
<git diff output>

--- STANDARDS SOURCES ---
<AGENTS.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md / CONTEXT-MAP.md,
applicable accepted ADRs, CONTRIBUTING.md>

--- SPEC slice — commit messages ---
### <sha1> <subject1>
<body1>
### <sha2> <subject2>
<body2>
... (exactly N entries)

--- SPEC SOURCES ---
<task file Acceptance Criteria + Plan, originating spec, parent PRD,
originating issue body if fetched — or "no spec source provided">

--- TOOLING NOTE ---
<list of machine-enforced configs found — skip what they already check>

=== END HANDOFF ===
```

After write, print `wc -l <path>` alongside the path so the user has a sanity-check value. Verify the handoff-integrity gate from Step 1 still holds: the commit-message section has exactly `N` `### <sha>` entries.

Advise the user to add `.agentic/reviews/` to `.gitignore` if it isn't already — handoffs are ephemeral audit artifacts, not committed history.

Step 5 — review. Apply the two axes with discipline. Read the diff once, classify each finding into exactly one axis before adding it to the report.

**Standards axis — report only:**
- Bugs (null/undefined paths, off-by-one, race conditions, broken invariants, wrong types, unhandled errors).
- Coupling (modules that shouldn't know about each other, leaked abstractions, hidden globals).
- Edge cases (empty inputs, large inputs, concurrent access, unicode, paths with spaces, missing files, permission errors).
- Diff violations of any standards source from Step 2 (AGENTS.md / ARCHITECTURE.md / GUIDELINES.md / CONTEXT.md / accepted ADRs / CONTRIBUTING.md).
- Vocabulary drift (paraphrasing canonical CONTEXT.md nouns).
- Skip what tooling already enforces (lint, format, type-check) — listed under TOOLING NOTE.

**Spec axis — report only:**
- (a) Requirements the spec asked for that are missing or partial.
- (b) Behaviour in the diff that wasn't asked for (scope creep).
- (c) Requirements that look implemented but where the implementation looks wrong against the spec line.
- Quote the spec line for each finding.
- If Spec was marked `no spec source provided`, report exactly that and report no Spec findings.

**Classification rule when a finding could belong to either axis:** route it to the axis whose source **defines** the constraint.
- "function returns undefined on empty input" → Standards (a code-quality bug).
- "function returns undefined on empty input, but the spec required `[]`" → Spec (the spec line is what's broken).

When in genuine doubt, place it in Standards.

Step 6 — output. Print findings under two headings, in this exact order:

```
## Standards Findings

<one finding per line: `file:line: <severity>: <problem>. <fix>.`>
<severity is the literal word `Blocker`, `Concern`, or `Note` — no emoji>
<if no findings, write exactly: "no real issues found in this axis">

End axis line: `Standards: ship as-is` / `Standards: ship with the Concerns logged` / `Standards: don't ship until Blockers resolved`.

## Spec Findings

<one finding per line: `file:line: <severity>: <problem> (spec: <quoted-line>). <fix>.`>
<if Spec was skipped, write exactly: "Spec: skipped — no spec source provided" and produce no findings>

End axis line: `Spec: ship as-is` / `Spec: ship with the Concerns logged` / `Spec: don't ship until Blockers resolved` / `Spec: skipped — no spec source provided`.
```

End the reply with a one-line aggregate summary:

```
Aggregate: <N Standards Blockers, M Standards Concerns> / <P Spec Blockers, Q Spec Concerns>. Worst: <one-line quote of the highest-severity finding from either axis, or "none">. Audit trail: <path>.
```

Do NOT synthesize an overall "approve" verdict. §10 frames the review as adversarial; the senior engineer weighs the findings.

Step 7 — recommend escalation when binding-doc findings exist. Scan the Step 6 output. For each Standards-axis finding (Blocker, Concern, or Note), check whether its `file:line` reference resolves to any of:

- `AGENTS.md` or `CLAUDE.md` at the repo root
- `ARCHITECTURE.md` at the repo root
- `GUIDELINES.md` at the repo root
- `CONTEXT.md` at the repo root or any per-context `CONTEXT.md`
- any file under `doc/adr/`

If at least one such finding exists, print verbatim:

```
Binding-doc finding(s) detected on the Standards axis. The single-session reviewer can downgrade premise-critical findings on these files (axis-bleed). Recommend re-running each binding-doc finding under fresh context via the Optional Escalation block at the bottom of this skill before merging.
```

If no Standards finding touches a binding doc, Step 7 is silent — emit nothing.

This step is the Option β escalation gate. The decision and the N=3 axis-bleed audit it rests on are recorded in git history (archived tasks 0002 and 0003 — recover via `git log --diff-filter=D -- doc/tasks/`).

**Optional escalation — true fresh-context review via subagent (explicit user-spawned).**
If the user wants the §10 ideal (a reviewer with no inherited bias), tell them after Step 6 / Step 7:

```
For a fresh-context review, spawn the bundled Codex reviewer subagent against the audit-trail file.

1. If the project was installed with agentic, the bundled reviewer should already exist at:
     .codex/agents/fresh-context-reviewer.toml

   If it is missing, create it with `/ad-subagent` or a standalone TOML file at one of:
     ~/.codex/agents/fresh-context-reviewer.toml          (personal — shared across all projects)
     .codex/agents/fresh-context-reviewer.toml            (project-scoped — committed with the repo)

   Minimum custom body (per developers.openai.com/codex/subagents). Required fields:
   `name`, `description`, `developer_instructions`. Optional fields: `model`,
   `model_reasoning_effort`, `sandbox_mode`. NOTE on TOML indentation: the
   triple-quoted `developer_instructions` string preserves leading whitespace
   verbatim, so dedent the body to column 0 when copying — do not carry the
   display indentation below.

       name = "fresh-context-reviewer"
       description = "Adversarial §10 reviewer. Reads only the handoff file."
       model = "gpt-5.4"                   # optional — omit to inherit parent session
       model_reasoning_effort = "high"     # optional
       sandbox_mode = "read-only"          # optional
       developer_instructions = """
   Read only the handoff file the user passes. No prior context.
   Report findings under ## Standards Findings and ## Spec Findings.
   Each finding one line: file:line: <severity>: <problem>. <fix>.
   Severity is the literal word Blocker, Concern, or Note.
   Do NOT synthesize an "approve" verdict.
   """

2. From Codex, explicitly spawn it against the audit-trail file:

     > spawn the fresh-context-reviewer agent. Read <audit-path>.

The subagent loads only the handoff file, so it has no inherited context from this
session. Requires Codex subagent support (see developers.openai.com/codex/subagents).
NOTE: the [agents] block in ~/.codex/config.toml is for global subagent
settings (max_threads, max_depth) only — not for declaring individual
subagents.
```

Do not silently spawn the agent yourself. The user must explicitly request the escalation.
</instructions>

<output_contract>
- One audit-trail file at `.agentic/reviews/<ISO-timestamp>-<scope-slug>.md` carrying the diff plus assembled Standards + Spec context.
- One review reply in the current session with findings under `## Standards Findings` and `## Spec Findings`, each axis with its own end-line verdict (`ship as-is` / `ship with the Concerns logged` / `don't ship until Blockers resolved` / `skipped — no spec source provided`).
- One aggregate summary line at the end with axis counts, worst finding, and audit-trail path.
- One Step 7 escalation recommendation line IF any Standards-axis finding touches a binding doc (AGENTS / ARCHITECTURE / GUIDELINES / CONTEXT / ADR). Silent otherwise.
- No "approve" verdict. No `/clear` choreography. No silent subagent spawn; the bundled subagent is for explicit escalation against the audit-trail file.
</output_contract>

## Next

- Address every Standards Blocker before merge — that's the code-quality hard gate. Re-run `ad-review` on the fix to confirm it cleared.
- Address Spec Blockers next — implementation-vs-spec drift is the second hard gate.
- Each Concern (from either axis) becomes a follow-up `ad-task`; do not let them silently accumulate.
- Notes are informational; close them out in the original task's `Notes` log if relevant.
- If the Spec axis was skipped, decide whether an `ad-spec` is overdue — work without a spec means future reviews are Standards-only.
- For maximum §10 fidelity on Codex, escalate to a user-spawned reviewer subagent against the persisted audit-trail file.
- Once both axes are clear: merge per project conventions.
