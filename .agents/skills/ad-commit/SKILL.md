---
name: ad-commit
description: Atomic Conventional Commits with DCO `Signed-off-by` sign-off. Four phases — scope intake, stage-split when concerns mix, draft message in Conventional Commits format, sign + write. Stage-split is interactive; identity comes from `git config user.name` / `user.email`. No `Co-Authored-By`. Helper posture, not blocker. Triggers on "commit this", "stage and commit", "atomic commit", "Conventional Commit", "sign off", "DCO", "split this commit", "/ad-commit".
summary: Atomic Conventional Commits with DCO `Signed-off-by` sign-off. Four phases — scope intake, stage-split when concerns mix, draft message in Conventional Commits format, sign + write. Helper posture, not blocker.
---

<background_information>
Implements ADR-0023 (`doc/adr/0023-agentic-commit-skill.md`). Drafts atomic Conventional Commits with DCO `Signed-off-by` sign-off. Helper, not blocker — the senior engineer keeps decision authority.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions committing, staging, or signing off, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when the user wants to land changes as commits and the working tree has staged or unstaged changes worth committing, or the user asks for "commit", "stage", "split this commit", "sign off".

Route elsewhere when:
- The user wants to open a PR after committing → continue with `ad-pr` after this skill finishes.
- The user wants to merge an open PR → `ad-merge`.
- The diff is unfinished work that should not land yet → say so; do not commit speculatively.

Phase 1 — scope intake. Read the working tree:

```
git status --short
git diff --staged
git diff
```

Classify concerns. Group changes by Conventional Commits type (`feat:`, `fix:`, `chore:`, `docs:`, `refactor:`, `test:`, `build:`, `ci:`, `perf:`, `style:`, `revert:`), subsystem (directory / module), and causal relationship (a mechanical rename and its callers are one concern; a `feat:` and an unrelated `fix:` are two).

Decision:
- Single concern → proceed to Phase 3.
- Multiple concerns → present a numbered list and ask the user to confirm a stage-split plan. Example:

```
Detected 3 concerns:
  1. feat(auth): add login flow (src/auth/, test/auth.test.js)
  2. fix(cli): trim trailing newline (src/index.js)
  3. docs(readme): refresh skill table (README.md)

Proceed concern-by-concern? (y/n, or "bundle them all into one commit")
```

When the user says "bundle them" or you are in genuine doubt, surface the call and proceed with their decision. Senior engineer keeps authority.

Phase 2 — stage-split (when needed). For each concern, in user-confirmed order:
1. Stage exactly the files (or hunks) for this concern. Use `git add <path>` for whole files; `git add -p <path>` when a hunk-level split is needed. Never `git add -A` / `git add .` when concerns are mixed.
2. Verify with `git diff --staged` — only this concern's changes are present.
3. Draft + write the commit (Phases 3 + 4 below).
4. Repeat for the next concern.

If a partial-file hunk split is too complex, surface the call: either accept the bundled commit for that file or ask the user to split the file edit first.

Phase 3 — draft message. Conventional Commits 1.0.0 format:

```
<type>(<scope>?): <subject>

<body — explains WHY when non-obvious; references task/ADR/issue>
```

Subject rules: ≤72 chars; imperative mood ("add", not "added"); no trailing period; lowercase after the colon; scope optional but recommended.

Body rules: explain why when non-obvious; reference task / ADR / issue (e.g. `Closes task-0025`, `Per ADR-0023`, `Fixes #42`); wrap at ~72 chars; blank line between paragraphs; no trailing summary of what the diff shows.

Type guide: `feat:` new user-visible capability; `fix:` bug fix; `chore:` maintenance; `docs:` documentation; `refactor:` no behavior change; `test:` tests only; `build:` build / packaging; `ci:` CI config; `perf:` performance; `style:` formatting; `revert:` reverts a prior commit.

Breaking changes: add `!` after the type/scope and a `BREAKING CHANGE:` footer paragraph.

Phase 4 — sign + write. Resolve identity:

```
git config user.name
git config user.email
```

If either is unset, stop and ask the user to configure them. DCO sign-off requires real attribution; the skill will not invent it.

Append the trailer (always last line, blank line before it):

```
Signed-off-by: <Name> <email>
```

Do not add a `Co-Authored-By:` trailer (ADR-0023 §Decision 1 — DCO only).

Write the commit using a HEREDOC so multi-line body content is preserved verbatim:

```
git commit -m "$(cat <<'EOF'
<type>(<scope>): <subject>

<body paragraph 1>

<body paragraph 2>

Signed-off-by: <Name> <email>
EOF
)"
```

Do not pass `--no-verify` or `--no-gpg-sign`. Pre-commit and pre-push hooks run; failures surface and the skill stops so the user can fix the underlying issue.

If a hook fails: investigate the underlying cause; do not skip the hook; fix the issue, re-stage if needed, and run `git commit` again (new commit; do not amend unless the user explicitly asks).
</instructions>

<output_contract>
The output is one or more atomic commits on the current branch. Each commit:
- Single concern.
- Conventional Commits format with valid type.
- Subject imperative + ≤72 chars.
- Body explains *why* when non-obvious; references task / ADR / issue when one exists.
- `Signed-off-by: <Name> <email>` trailer.
- No `Co-Authored-By` trailer.
- No skipped hooks.
</output_contract>

## Next

- After commits land: `ad-pr` to open the PR.
- After PR open and CI green: `ad-merge` to evaluate and merge.
- If a hook failure exposed recurring drift: `ad-audit`.
