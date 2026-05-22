---
name: ad-pr
description: Open a GitHub pull request with a uniform body shape. Four phases — preflight (`gh` auth + branch pushed), scope assembly (commits + diff vs base), draft body (Summary / Test plan / Links), open + report URL. Title format = Conventional Commits, type inferred from the dominant commit type in the range. `gh` CLI soft-fail with install hint. Triggers on "open a PR", "create a pull request", "submit a PR", "gh pr create", "/ad-pr".
summary: Open a GitHub pull request with a uniform body shape (Summary / Test plan / Links). Four phases — preflight (`gh` auth + branch pushed), scope assembly, draft body, open + report URL. Title format = Conventional Commits.
---

<background_information>
Implements ADR-0024 (`doc/adr/0024-agentic-pr-skill.md`). Opens a PR via `gh pr create` with a uniform body shape (Summary / Test plan / Links). Helper, not blocker — warnings surface but do not refuse when the user authorizes.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions opening a PR or submitting changes for review, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when one or more atomic commits exist on the current branch ahead of the base, or the user asks to "open a PR", "submit a PR", "create a pull request".

Route elsewhere when:
- The branch has uncommitted or unsplit work → `ad-commit` first.
- The user wants to merge an already-open PR → `ad-merge`.

Phase 1 — preflight. Check `gh` is installed and authenticated:

```
gh --version
gh auth status
```

If `gh` is missing, surface: "`gh` CLI not installed. Install: https://cli.github.com/ then run `gh auth login`. This skill cannot open the PR without it." Soft-fail — do not silently fall back to `git push` only.

If `gh auth status` fails, surface the same hint with `gh auth login`.

Check the branch is pushed:

```
git rev-parse --abbrev-ref @{u}
git rev-list --count <base>..HEAD
```

If no upstream is set or the branch is unpushed, prompt: "Branch not pushed to origin. Run `git push -u origin <branch>` first? (y/n)". On confirm, run the push and continue.

Detect the base branch:

```
gh repo view --json defaultBranchRef --jq .defaultBranchRef.name
```

Default to `main` if the call fails.

Phase 2 — scope assembly. Gather context:

```
git log <base>..HEAD --pretty='%h %s'
git diff --stat <base>...HEAD
```

Infer the PR title:
- Dominant Conventional Commits type across `<base>..HEAD`. If one commit, use its type / scope / subject directly. If multiple commits and one type dominates, use that. If mixed, surface to the user: "Branch mixes `feat:` + `fix:`. Use which type for the PR title?"
- Scope = most common scope across the commits (or empty if inconsistent).
- Subject synthesized from the changes, ≤70 chars, imperative.

Detect back-links:
- Scan commit message bodies for `task-NNNN`, `ADR-NNNN`, `spec-NNNN`, `#<issue>`, `Closes`, `Fixes`, `Per`, `See`.
- Scan changed file paths for `doc/tasks/NNNN-*.md`, `doc/adr/NNNN-*.md`, `doc/specs/NNNN-*.md`.

Phase 3 — draft body. Template:

```
## Summary
- <1-3 bullets — the WHY, not the what>

## Test plan
- [ ] <test 1>
- [ ] <test 2>

## Links
- <task / spec / ADR / issue back-links>
```

Rules:
- Summary: explain motivation. The commit list and diff already show the what. 1-3 bullets max.
- Test plan: concrete steps a reviewer can run. `npm test`, manual smoke, integration check. Skip the section if there is genuinely nothing to test (pure docs PR). Never fabricate test items.
- Links: every detected back-link from Phase 2. Skip the section if there are none. Never invent links.

Surface the draft to the user for approval before opening.

Phase 4 — open + report. Call `gh pr create` with HEREDOC body:

```
gh pr create --title "<type>(<scope>): <subject>" --body "$(cat <<'EOF'
## Summary
- ...

## Test plan
- [ ] ...

## Links
- task-NNNN, ADR-NNNN, #issue
EOF
)"
```

Capture the returned PR URL and report it to the user.

If `gh pr create` fails (no GitHub remote, branch already has an open PR, etc.), surface the error verbatim and stop. Do not retry blindly.
</instructions>

<output_contract>
The output is an opened PR on GitHub. The skill returns the PR URL.
</output_contract>

## Next

- After PR opens and CI starts: monitor via `gh pr checks`.
- When CI is green: `ad-merge` to evaluate and merge.
- If a fresh-context review is wanted before merge: `ad-review` (WORKFLOW §10).
- If preflight surfaced "no CI workflow file": `ad-hooks` (when installed) covers local gates; remote CI is a separate concern outside this skill.
