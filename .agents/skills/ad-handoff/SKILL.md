---
name: ad-handoff
description: Compact the current session into a handoff document a fresh agent can pick up from. Saves to the OS temp dir (never the repo). Captures live state — current branch, open artifacts, unresolved decisions, in-progress diff, recent errors — references existing artifacts (PRD / spec / task / ADR) by path instead of duplicating them, and lists suggested next skills. Redacts secrets before writing. Triggers on "handoff", "hand off this session", "compact this conversation", "save context for next session", "pass to another agent", "wrap up the session", "context exhausted", "/clear", "/ad-handoff".
summary: Compact current session into a handoff doc in the OS temp dir. Rebuilds the work as a done/open roadmap checklist, sweeps for asks that never landed, reports repo hygiene without acting on it, binds the next agent to ad-philosophy, references artifacts by path, redacts secrets. Never commits.
---

<background_information>
Process scaffold for compacting a long or near-exhausted session into a handoff document. Output is a single markdown file in the OS temp directory; the next session reads it and continues. Helper, not blocker — the senior engineer keeps decision authority over what survives the compaction.

The skill exists because long sessions accumulate inherited bias (WORKFLOW §12 — "almost right" failures compound) and because session-window pressure forces ad-hoc summarisation that loses the parts the next agent actually needs. Handoffs replace ad-hoc summarisation with a structured artifact.

Codex subagents and sessions do not share the parent conversation history by magic, and `/clear` discards local context. Handoff via the OS temp dir is the durable bridge between Codex sessions or explicit subagent work — treat it as load-bearing, not optional. Codex auto-trigger on description keywords is less mature than Claude Code's; if auto-invocation does not fire when the user mentions handing off, compacting, or `/clear`, invoke this skill manually.
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
- Repo hygiene — `git worktree list`, `git branch --merged <default>`, `git branch --no-merged <default>`, stray untracked files at the repo root. Delete the merged branches: `git branch -d <name>` refuses anything not fully merged, so git itself is the guard, and a merged branch's commits survive on the default branch and in the reflog — the blast radius WORKFLOW §7 asks you to match is near zero. Run it and report what was deleted. Everything else is reported with its command and never run: unmerged branches (including squash-merged ones, which git cannot distinguish from genuinely unmerged), worktrees, and stray files get `git branch -D <name>` / `git worktree remove <path>` written next to them. Those are the destructive cases — `-D` discards commits. Known limit, tested rather than assumed: squash-merged branches cannot be detected — `git branch --merged`, `git cherry` and a three-dot diff all miss them, because squashing rewrites the patch-id. Report as ambiguous; a heuristic that guesses here would authorise deleting real work.
- What the next agent should do first — one sentence, imperative.

References, not copies. `Spec: doc/specs/0007-foo.md` beats pasting the spec. The next agent will read those files fresh.

Step 2 — reconstruct the roadmap. State is not a plan: a snapshot says where the files are, not what arc the work is in, and an agent that cannot see the arc re-derives it badly and at cost.

Walk the session start to finish and rebuild it as one checklist, done and open together, in dependency order. Every line stands alone — a reader who never saw this session understands the item without opening another file. Use `- [x]` for landed work, naming the artifact that proves it (commit subject, test name, published version) rather than the claim; `- [ ]` for open work, naming the blocking condition and its owner.

Then check the order against the session's stated priorities. If the user corrected course, ranked items, or rejected an approach, the roadmap carries the corrected order — replaying the original plan re-introduces the mistake the correction fixed. State the alignment; if an open item no longer matches a stated priority, say so rather than carrying it silently.

Step 3 — sweep for asks that never landed. Re-read the session for requests that became neither work, nor an artifact, nor an explicit decision to skip. These are the highest-loss items in a handoff: important enough to say out loud, invisible in `git status`. Look for asks deferred mid-turn ("park that", "later"), superseded by a more urgent thread, answered partially, or acknowledged then buried. Record each in the user's own framing with its disposition — not started, stopped at X, or dropped deliberately because Y. An empty sweep is a real outcome; write "none" rather than inventing entries.

Step 4 — redact. Before writing, scrub:
- API keys, tokens, JWTs, OAuth client secrets — replace with `<REDACTED:type>`.
- Environment variable values (`.env`, `process.env.X` payloads, anything credential-shaped).
- Personally identifiable information beyond what is in the repo's git config.
- Absolute paths containing user home directory content that is not part of this repo (the repo's own absolute path may stay).

If a value is needed by the next session but is sensitive, write a placeholder + a one-line note on where to fetch it (`pull from 1Password vault "X"`, `re-export from env`).

Step 5 — resolve handoff path:

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

Step 6 — write the handoff. File shape:

```
# Handoff — <slug> — <ISO date>

**Repo:** <repo path>
**Branch:** <current branch>
**Started from:** <PR # / task # / spec # if known, else "ad-hoc work">

## First action

Invoke `ad-philosophy` explicitly, then <one imperative sentence naming the concrete next move>. Per ADR-0044 the explicit invocation forces an applied-binding statement — each of `ad-philosophy`'s behaviors named against this task's files and commands before work starts. A fresh agent inheriting a mid-flight session is exactly where posture is assumed and then quietly dropped. This instruction stands in for the user's: treat it as an explicit invocation and produce the applied-binding statement, not as the auto-load path that skips it.

It binds posture, not method — grounding, hypothesis discipline, test-first discipline and fresh-context review live in their own skills and are not carried by that statement. The next section states them outright.

## Working rules

**These are restated, not referenced, and that is deliberate.** A handoff is an ephemeral `$TMPDIR` artifact read once by an agent with no history; a pointer it may not follow is worth less than four lines it cannot miss. The anti-duplication rule protects versioned documents from drifting copies — this file is deleted after it is read.

**Reporting to the engineer.** They are the boss, not the co-pilot, and did not read the files you read. Lead with the conclusion — what happened, what it means, what comes next — in plain terms first, technical detail after. Write so they grasp it in about thirty seconds; that is a target for how the report lands, not a length limit, and clarity beats compression when the two pull apart. Translate artifacts into what they mean and what to do — a pasted diff or metric is not a report.

**Deciding vs asking.** Default is decide. A grounded happy path, a single-criterion winner, an established pattern, a green deterministic gate: take it. Ask only for design and taste, irreversible or high-blast-radius actions, genuine ties, insufficient evidence, or a fuzzy spec — and bring one question with the recommended answer first and why the alternatives are weaker. Never a survey.

**Grounding before building.** Non-trivial change gets `ad-ground` first — official docs, validated implementation references, in-repo patterns, git history — then a stated happy path. Deviating needs an irrefutable reason, stated.

**Hypotheses before fixes.** A bug gets 3–5 ranked falsifiable hypotheses before any is tested, each naming the prediction that would kill it. Single-hypothesis debugging anchors on the first plausible idea and is the most common failure mode after having no feedback loop. `ad-diagnose` carries the loop.

**Validate before correcting.** Never fix on a claim you have not reproduced — including one from a reviewer, a prior session, or this handoff. Re-run it and state what you observed, or say UNVERIFIED. An "N of M" claim needs the command that reproduces the enumeration **and** the false positives in its output named — or a statement that none were found and how that was checked.

**Tests before the fix is trusted.** When the behaviour is expressible as a test, write the failing test first and let it drive the change — one test at a time, verifying behaviour through public interfaces, never bulk-writing tests after the fact. Every bug that gets fixed gets a regression test that fails without the fix.

**Spike when the technique is the unknown.** If the uncertainty is *how* rather than *what* — several plausible approaches and no way to pick from reading — prove it with a staged spike and a golden fixture (`ad-spike`) before correcting anything. Only when that is genuinely the situation; a spike for a problem whose technique is already settled is wasted motion.

**Review before landing.** `ad-review` on the range before anything merges, two axes, fresh context. The agent that wrote the code is biased about it.

## Roadmap

- [x] <landed item> — <the artifact that proves it>
- [ ] <open item> — <blocking condition, owner>

**Priority alignment:** <how this order reflects the user's stated priorities and any course correction, or "no correction stated this session">

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

## Repo hygiene

- Deleted (merged, safe): <name>, <name> — removed with `git branch -d`; commits live on <default> and in the reflog. (or "none")
- Left for you: <name> (<unmerged | squash-merged — git cannot distinguish>) — `git branch -D <name>` (or "none")
- Worktrees: <path> — `git worktree remove <path>` (or "one, clean")
- Stray files at root: <path> — <gitignore it, or move it out> (or "none")

## Asks that never landed

- <the user's request, in their framing> — <not started | stopped at X | dropped deliberately because Y>

(Write "none" when the sweep is genuinely empty.)

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
- Spec unclear → `ad-grill-me`.
- About to land work → `ad-commit` → `ad-pr` → `ad-merge`.
- Ready for fresh-context review → `ad-review main..HEAD` (Codex writes an audit-trail handoff, then can explicitly spawn the bundled reviewer subagent against that file when the user asks).

Write the file. Print the absolute path so the user can paste it into the next session.

Step 7 — hand off. Tell the user:
1. The handoff path.
2. The roadmap, rendered inline in the reply — the user reads the plan here, not by opening a file. Conclusion first, no preamble: done and open in one list, graspable in about thirty seconds without opening the file.
3. What hygiene deleted (merged branches only) and what it left for them, each with its command.
4. Any ask the sweep found unlanded — surface these even when inconvenient. A handoff that quietly drops a request is worse than none, because it looks complete.
5. The single recommended first action, and the suggested-skills list verbatim.

Do not auto-execute `/clear` or anything destructive. The user decides when to discard the current session.
</instructions>

<output_contract>
- A single markdown file at `${TMPDIR:-/tmp}/agentic-handoffs/<ISO>-<slug>.md`.
- File contains only the sections above, in that order. Omitted sections are removed entirely (not left as `## Heading\n\nN/A`). `Roadmap` and `Asks that never landed` are never omitted — an empty sweep is written as "none", because silence and emptiness are indistinguishable to the next agent.
- The roadmap is one checklist covering done and open work in dependency order, each line self-sufficient, with an explicit priority-alignment line.
- Repo hygiene deletes fully-merged branches with `git branch -d` and reports them; everything destructive (`-D`, worktree removal, file deletion) is reported with its command and never run. Confirmation is matched to blast radius (WORKFLOW §7), not waived and not inflated.
- The `Working rules` section states the rules outright rather than pointing at them; `ad-philosophy` binds posture and is never presented as covering grounding, hypothesis, test or review discipline.
- References artifacts by path; never duplicates their content.
- Secrets are replaced with `<REDACTED:type>` placeholders.
- Suggested skills are drawn from the installed `ad-*` set and each line carries a one-clause rationale.
- The user receives the absolute path of the file and the recommended first action; no destructive session ops are performed.
</output_contract>

## Next

- Paste the handoff path into the next session or hand it to an explicit subagent: `cat /tmp/agentic-handoffs/<file>.md`, then `/clear` or spawn the relevant agent with the path.
- If the handoff surfaced an unresolved decision worth recording: open an ADR with `ad-adr` before the next agent picks the work up.
- If the handoff surfaced a vocabulary drift (a term you kept paraphrasing): `ad-domain` to land it in `CONTEXT.md` so the next agent inherits the canonical noun.
