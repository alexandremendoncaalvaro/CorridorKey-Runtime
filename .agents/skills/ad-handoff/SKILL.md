---
name: ad-handoff
description: Compact the current session into a handoff document a fresh agent can pick up from. Saves to the OS temp dir (never the repo). Captures live state — current branch, open artifacts, unresolved decisions, in-progress diff, recent errors — references existing artifacts (PRD / spec / task / ADR) by path instead of duplicating them, and lists suggested next skills. Redacts secrets before writing. Triggers on "handoff", "hand off this session", "compact this conversation", "save context for next session", "pass to another agent", "wrap up the session", "context exhausted", "/clear", "/ad-handoff".
summary: Compact current session into a handoff doc in the OS temp dir. Captures live state, references artifacts by path (no duplication), suggests next skills, redacts secrets. Ephemeral by design — never commits to the repo.
---

<background_information>
Process scaffold for compacting a long or near-exhausted session into a handoff document. Output is a single markdown file in the OS temp directory; the next session reads it and continues. Helper, not blocker — the senior engineer keeps decision authority over what survives the compaction.

The skill exists because long sessions accumulate inherited bias (WORKFLOW §12 — "almost right" failures compound) and because session-window pressure forces ad-hoc summarisation that loses the parts the next agent actually needs. Handoffs replace ad-hoc summarisation with a structured artifact.

Codex has no subagent primitive and no shared session memory across `/clear`. Handoff via the OS temp dir is the only durable bridge between two Codex sessions — treat it as load-bearing, not optional. Codex auto-trigger on description keywords is less mature than Claude Code's; if auto-invocation does not fire when the user mentions handing off, compacting, or `/clear`, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when at least one holds:
- The user says "handoff", "hand off", "wrap up", "compact this", "save context", "pass to another agent", "I need to `/clear`".
- The session has run long enough that context-window pressure is imminent or stated.
- The user is about to switch agents (Codex → Claude Code, or back) and wants the work to continue.

Route elsewhere when:
- The work is finishable in the current turn → finish it; do not pre-emptively hand off.
- The handoff target is a specific artifact (PRD / spec / task / ADR) → write that artifact via the matching `ad-*` skill instead. Handoffs do not replace persistent decision records.
- The user wants to commit work-in-progress → `ad-commit` or finish the change first.

Step 1 — collect live state. Do not duplicate content that already lives in repo artifacts; reference those by path.

Capture:
- Working tree — `git status --short`, current branch (`git branch --show-current`), divergence (`git log --oneline @{upstream}..HEAD` if a remote is set).
- In-progress diff — `git diff` + `git diff --staged`. If the combined diff is >300 lines, summarise per-file and tell the next agent to re-read it with `git diff`.
- Open artifacts — files touched but uncommitted; task file under `doc/tasks/` driving the work (if any); spec under `doc/specs/` it implements (if any); ADR being drafted under `doc/adr/` (if any).
- Unresolved decisions — questions the user posed that you have not answered; questions you posed that the user has not answered. One bullet per open question, with a recommended-answer line where you have one.
- Recent errors / hook failures — verbatim, last error message + which command produced it.
- What the next agent should do first — one sentence, imperative.

References, not copies. `Spec: doc/specs/0007-foo.md` beats pasting the spec. The next agent will read those files fresh.

Step 2 — redact. Before writing, scrub:
- API keys, tokens, JWTs, OAuth client secrets — replace with `<REDACTED:type>`.
- Environment variable values (`.env`, `process.env.X` payloads, anything credential-shaped).
- Personally identifiable information beyond what is in the repo's git config.
- Absolute paths containing user home directory content that is not part of this repo (the repo's own absolute path may stay).

If a value is needed by the next session but is sensitive, write a placeholder + a one-line note on where to fetch it (`pull from 1Password vault "X"`, `re-export from env`).

Step 3 — resolve handoff path:

```
TMP="${TMPDIR:-/tmp}"
DIR="$TMP/agentic-handoffs"
mkdir -p "$DIR"
ISO=$(date -u +%Y%m%dT%H%M%SZ)
SLUG=<from-arg-or-branch>
PATH_OUT="$DIR/$ISO-$SLUG.md"
```

Slug derivation, in priority order:
1. The argument the user passed (`ad-handoff merge-cleanup` → `merge-cleanup`).
2. Current branch name with non-alphanumeric chars replaced by `-`.
3. `session` as the fallback.

Never write inside the repo. Never add `.agentic/handoffs/` to the repo's `.gitignore`. Handoffs are per-session OS artifacts, not per-repo audit trail.

Step 4 — write the handoff. File shape:

```
# Handoff — <slug> — <ISO date>

**Repo:** <repo path>
**Branch:** <current branch>
**Started from:** <PR # / task # / spec # if known, else "ad-hoc work">

## What the next agent should do first

<one imperative sentence>

## State

- **Working tree:** <git status one-line summary>
- **Divergence:** <commits ahead/behind upstream, or "no remote tracked">
- **In-progress diff:** <line count + file count, or inline if small>

## Open artifacts

- Task: `doc/tasks/NNNN-<slug>.md` — <one-line status>
- Spec: `doc/specs/NNNN-<slug>.md` — <one-line status>
- ADR (drafting): `doc/adr/NNNN-<slug>.md` — <one-line status>
- Touched but uncommitted: <list of paths>

(Omit any subsection that does not apply. Do not invent references.)

## Unresolved decisions

- <question> — recommended: <answer>
- <question from user> — not yet answered

## Recent errors

<verbatim last error + command, or "none">

## Suggested skills for the next session

- `ad-<skill>` — <why this is the natural next move>
- `ad-<skill>` — <why>

## Notes

<anything that doesn't fit above — keep tight, one paragraph max>
```

Suggested-skills picks from the installed `ad-*` set. Common patterns:
- Mid-implementation, behavior expressible as test → `ad-tdd`.
- Stuck on a bug → `ad-diagnose`.
- Spec unclear → `ad-grill`.
- About to land work → `ad-commit` → `ad-pr` → `ad-merge`.
- Ready for fresh-context review → `ad-review main..HEAD` (Codex variant uses `/clear` + paste handoff since Codex has no subagent primitive — exactly the pattern this skill scaffolds).

Write the file. Print the absolute path so the user can paste it into the next session.

Step 5 — hand off. Tell the user:
1. The handoff path.
2. The single recommended first action for the next session.
3. The suggested-skills list, verbatim.

Do not auto-execute `/clear` or anything destructive. The user decides when to discard the current session.
</instructions>

<output_contract>
- A single markdown file at `${TMPDIR:-/tmp}/agentic-handoffs/<ISO>-<slug>.md`.
- File contains only the sections above, in that order. Omitted sections are removed entirely (not left as `## Heading\n\nN/A`).
- References artifacts by path; never duplicates their content.
- Secrets are replaced with `<REDACTED:type>` placeholders.
- Suggested skills are drawn from the installed `ad-*` set and each line carries a one-clause rationale.
- The user receives the absolute path of the file and the recommended first action; no destructive session ops are performed.
</output_contract>

## Next

- Paste the handoff path into the next session: `cat /tmp/agentic-handoffs/<file>.md` then `/clear`, then paste contents as the first message.
- If the handoff surfaced an unresolved decision worth recording: open an ADR with `ad-adr` before the next agent picks the work up.
- If the handoff surfaced a vocabulary drift (a term you kept paraphrasing): `ad-domain` to land it in `CONTEXT.md` so the next agent inherits the canonical noun.
