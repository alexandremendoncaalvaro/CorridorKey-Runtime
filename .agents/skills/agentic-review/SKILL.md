---
name: agentic-review
description: Fresh-context code review per WORKFLOW §10 — assemble the diff plus the relevant spec slice (AGENTS.md, applicable ADRs, the task's Acceptance Criteria), then print a /clear handoff for a clean session. Use when the user wants to review a diff, branch, PR, or recent commits against the project's spec, audit for bugs / coupling / edge cases / spec drift, or run a §10 senior-reviewing-junior pass. Adversarial framing — never emits an "approve" verdict.
---

<background_information>
Implements WORKFLOW §10 (Reviewer With Fresh Context). The current session is biased about the code it produced — the same reasoning that wrote it defends it. This skill assembles a clean handoff and the user re-loads it into a fresh context so the review reads as adversarial.

Codex has no native subagent primitive. Parity with §10 is preserved through a manual handoff: this skill prints the assembled handoff, then the user runs `/clear` and pastes the handoff back into the empty session. One extra UX step versus Claude Code's bundled subagent.
</background_information>

<instructions>
Step 0 — scope the review. Confirm what to review. Default scopes, in priority order:
1. User-named ref or PR (e.g. `agentic-review main..HEAD`, `agentic-review <commit-sha>`).
2. Current branch vs `main` (`git diff main...HEAD`).
3. Working-tree changes (`git diff` plus `git diff --staged`).

If no diff exists, stop and tell the user — there's nothing to review.

Step 1 — assemble the handoff. The fresh session will get only what you print here. No conversation history, no prior context.
- Diff for the chosen scope (`git diff <range>`). Use `--stat` first; if it spans >50 files, ask the user to narrow.
- `AGENTS.md` at the repo root, if present.
- `ARCHITECTURE.md` at the repo root, if present.
- Every ADR under `doc/adr/` with `Status: accepted` whose subject is touched by the diff. When in doubt, include rather than skip.
- Relevant task file under `doc/tasks/` — if the diff or recent commit messages reference `Task NNNN`, read its Acceptance Criteria and Plan.
- Recent commit messages for the range (`git log <range> --format=%B`).

Step 2 — print the handoff inline. Format the message exactly as below so the user can copy it as a single block. Do not summarize what you think the diff does.

```
=== AGENTIC-REVIEW HANDOFF ===

You are a senior engineer reviewing a junior PR with no prior context. The diff and the spec below are the only evidence. Do not infer history or trust the author's intent.

Review focus, in priority order:
1. Bugs — null/undefined paths, off-by-one, unhandled errors, race conditions, broken invariants.
2. Spec drift — does the diff contradict AGENTS.md, an accepted ADR, or the task Acceptance Criteria?
3. Coupling — modules that shouldn't know about each other, leaked abstractions.
4. Edge cases — empty inputs, large inputs, concurrent access, missing files, permission errors.
5. Test coverage — does any new behavior have a corresponding test?

Skip formatting, naming opinions, stylistic preferences. Skip praise.

Output: group findings by severity (Blocker / Concern / Note). Each finding one line: `file:line: <severity>: <problem>. <fix>.`

End with a one-line bottom-line: "Ship as-is", "Ship with the Concerns logged as follow-up tasks", or "Don't ship until Blockers resolved". Do NOT synthesize an "approve" verdict.

--- DIFF ---
<paste git diff output here>

--- SPEC SLICE ---
<paste AGENTS.md, ARCHITECTURE.md, applicable ADRs, task file Acceptance Criteria + Plan, recent commit messages>

=== END HANDOFF ===
```

Step 3 — instruct the user. After printing the handoff, output exactly this and stop:

```
Copy the handoff above. Run `/clear` to drop the current context. Paste the handoff into the empty session. Codex will produce the structured findings.
```

Do not proceed past this point in the current session.
</instructions>

<output_contract>
One inline message containing the assembled handoff (review instructions + diff + spec slice), followed by a short instruction telling the user to /clear and paste. No file is written. The current session does not produce findings — the fresh session does, after the user re-loads the handoff. This honors WORKFLOW §10 by enforcing context isolation through `/clear` rather than via a subagent primitive Codex lacks.
</output_contract>
