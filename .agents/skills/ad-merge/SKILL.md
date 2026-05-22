---
name: ad-merge
description: Evaluate and merge a GitHub pull request. Four phases — preflight (`gh` auth + PR resolution), evaluate (CI / fresh-context review / linked task / unresolved comments / mergeability), decision (CI green = hard gate; others = warnings yielding to user), merge via `gh pr merge` with auto-detected mode (squash / rebase / merge) and `--delete-branch`. Helper posture' ' — surfaces warnings, does not block on the senior engineer's judgment. Triggers on "merge this PR", "evaluate the PR", "is it mergeable", "gh pr merge", "/ad-merge".
summary: Evaluate and merge a GitHub pull request. Four phases — preflight, evaluate (CI / fresh-context review / linked task / unresolved comments / mergeability), decision (CI green = hard gate; others = warnings), merge with auto-detected mode + `--delete-branch`.
---

<background_information>
Implements ADR-0025 (`doc/adr/0025-agentic-merge-skill.md`). Evaluates a PR's mergeability and performs the merge via `gh pr merge`. CI green is the only hard gate; everything else surfaces as a warning the senior engineer decides on.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions merging a PR or evaluating mergeability, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when a PR is open against the repo and the user wants to land it, or the user asks "merge this PR", "evaluate the PR", "is it mergeable".

Route elsewhere when:
- No PR exists yet → `ad-pr` first.
- Commits are not on the branch yet → `ad-commit` first.

Phase 1 — preflight. Check `gh` is installed and authenticated:

```
gh --version
gh auth status
```

If absent or not authed, surface the install / `gh auth login` hint and stop (same soft-fail rule as `ad-pr`).

Resolve the target PR:
- If the user passed a PR number / URL, use it.
- Else `gh pr view --json number,headRefName,baseRefName` against the current branch. If that fails, surface: "No PR found for branch `<name>`. Open one with `ad-pr` first."

Phase 2 — evaluate. Run the structured check and report each line:

```
gh pr checks <num>
gh pr view <num> --json mergeable,mergeStateStatus,reviews,number,title,headRefName,baseRefName
gh api repos/:owner/:repo/pulls/<num>/comments
```

Findings format (pass / warn / fail):

```
CI status:           <pass | pending | fail>
Fresh-context review: <pass | warn — none found>
Linked task / ADR:   <pass | warn — none>
Unresolved comments: <pass | warn — N unresolved>
Mergeability:        <pass | dirty | blocked | behind>
```

Fresh-context review check — scan for either a file under `.agentic/reviews/*` whose name references the PR's commit range or number, or a `gh pr view --json reviews` entry with `state: APPROVED`.

Linked task / ADR — scan commit message bodies under `<base>..HEAD` and the PR body for `task-NNNN`, `ADR-NNNN`, `spec-NNNN`, `#<issue>`, `Closes`, `Fixes`.

Unresolved comments — count entries from `gh api` that lack a `resolved` flag or carry an in-progress thread state.

Phase 3 — decision. Apply the bar:

- CI failing → hard stop. Refuse to merge until CI is green, unless the user explicitly overrides ("merge anyway"). On override, log a loud warning that this is a deliberate CI-failing merge and the responsibility is the user's. Per ADR-0025 §3, even the hard gate yields to explicit user authorization, but the override is surfaced visibly.
- CI pending → ask the user: wait for CI, or proceed anyway? Default = wait.
- CI green + warnings (no fresh-context review / no linked task / unresolved comments) → surface each warning, ask the user to confirm the merge, proceed on confirm.
- All green → proceed.

State the decision back to the user before Phase 4 so they can interject.

Phase 4 — merge. Detect repo's allowed merge modes:

```
gh repo view --json mergeCommitAllowed,squashMergeAllowed,rebaseMergeAllowed
```

Decision tree:
- Exactly one mode allowed → use it.
- Multiple modes allowed → ask the user: "Repo allows squash / rebase / merge-commit. Pick one." Wait for their choice.
- None allowed (rare) → surface the policy error and stop.

Run the merge:

```
gh pr merge <num> --squash --delete-branch
```

(replace `--squash` with `--rebase` or `--merge` per the chosen mode).

`--delete-branch` by default for feature branches (`feat/*`, `fix/*`, `chore/*`, `docs/*`, `refactor/*`). Skip `--delete-branch` if the source branch is a long-lived integration branch (e.g., `cli`, `develop`, `release/*`).

Capture and report the merge commit URL.
</instructions>

<output_contract>
The output is a merged PR. The skill returns:
- The merge commit URL.
- A one-line summary (`<count> commits merged into <base> via <mode>`).
- The list of warnings the user proceeded past (if any), for the audit trail.
</output_contract>

## Next

- After merge: pull the latest base locally (`git checkout <base> && git pull`).
- If the merge surfaced a recurring drift: `ad-audit` for systemic gaps, or update `WORKFLOW.md` §10 / §11.
- If the merge closed a task: confirm the task file's Status is `done` and the Notes log captures the merge commit URL.
- If the merge shipped a binding decision worth recording: `ad-adr` (three-criteria rule).
