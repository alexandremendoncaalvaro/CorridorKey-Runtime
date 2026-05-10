---
name: agentic-next
description: Survey the project's state across the four-layer artifact stack and recommend prioritized next actions, modeled on `flutter doctor`. Use when the user asks "what's next", "next step", "where am I", "project status", "doctor", "what should I do", "audit my workflow", or whenever a navigation aid is needed mid-flow. Read-only; complements `agentic-audit` (drift detection, a different question). Profile-aware — `poc` suppresses Layer 2 / 3 noise, `team` / `mature` run the full survey.
allowed-tools: Read, Glob, Grep, Bash
---

# /agentic-next

Read-only state survey + prioritized next-action recommendations. Mirrors `flutter doctor` shape: layer-by-layer status + concrete fix per finding. Complements `agentic-audit` — audit answers "is anything wrong?", next answers "what should I do?".

The skill writes nothing. Output is recommendations the user copies into the next conversation turn or the next CLI invocation.

## Step 0 — Read state

Detect baseline:

* Profile + kit version: read `.claude/agentic-state.json` and `.agents/agentic-state.json` if present. Profile defaults to `team` per ADR-0013 when state file missing or no profile field.
* Filesystem signals at the repo root: `AGENTS.md` / `CLAUDE.md`, `ARCHITECTURE.md`, `DESIGN.md`, `WORKFLOW.md`, `README.md`, `package.json` / `pyproject.toml` / `Cargo.toml` / `go.mod`, `.husky/` / `lefthook.yml` / `.pre-commit-config.yaml`, `.github/workflows/`, `.git/HEAD` (current branch).
* Per-artifact directories: list `doc/specs/`, `doc/adr/`, `doc/tasks/`. Read each file's frontmatter (`Status:`, `Created:`, `Spec ref:` for tasks) but **not** the full body — survey is fast and broad.
* Git state: current branch, commits ahead of `main` (`git rev-list --count main..HEAD`), unpushed commits, working-tree dirtiness.

Do not parse skill bodies. Do not run tests. Do not invoke other skills. The survey is shallow by design.

## Step 1 — Layer-by-layer status

Render four sections in this exact order. For each section, list what is present, what is in flight, what is missing or stale. Use words for status (`present`, `in flight`, `missing`, `stale`) — no emoji.

**Layer 1 — Constitution.**
- `AGENTS.md` (or `CLAUDE.md`) present?
- `WORKFLOW.md` present? (kit-shipped — should always be there)
- `ARCHITECTURE.md` present?
- `DESIGN.md` present? (frontend projects only)

**Layer 2 — Specs (`doc/specs/`).**

For each spec file, report `Status` and the count of tasks whose `Spec ref` field points at it:

```
0001-auth-flow.md (accepted, 0 implementing tasks)
0002-onboarding.md (shipped, 3 tasks done)
```

Flag specs with `Status: accepted` and zero implementing tasks — that is the most common stuck state.

**Layer 3 — Plans / Decisions.**

`doc/adr/` — count by status: `proposed`, `accepted`, `deprecated`, `superseded`. Flag any `proposed` ADRs explicitly with their slug — they need a decision.

`doc/tasks/` — count by status: `proposed`, `in-progress`, `blocked`, `done`. List in-progress and blocked tasks with their slugs and `Spec ref`. Flag tasks with no `Spec ref` and no `Board ref` as orphans (no clear scope tie).

**Layer 4 — Code.**
- Branch: `<name>` (`<n>` commits ahead of `main` if applicable).
- Tests: wired? (presence of `npm test` script / `pytest` / `cargo test` / `go test ./...`).
- Hooks: wired? (presence of `.husky/`, `lefthook.yml`, `.pre-commit-config.yaml`, or active `.git/hooks/` scripts).
- CI: wired? (presence of `.github/workflows/`, `.gitlab-ci.yml`, `.circleci/`).

## Step 2 — Cross-cut signals

A few signals do not belong to one layer:

- **Pending fresh-context review.** If branch is ≥1 commits ahead of `main` and no `.agentic/reviews/<ts>-*.md` exists for the current range, flag `agentic-review` as a recommendation.
- **Spec ↔ task reciprocity.** Tasks with non-empty `Spec ref` whose target spec does not exist → orphan task. Specs with `Status: accepted` or `shipped` and zero entries in their `Related → Tasks` list → spec without implementing tasks.
- **Profile vs install state.** Profile says one set of skills; state file lists another. Surface the divergence and recommend `agentic update` or `agentic profile set <name>`.
- **Stale state file.** `kitVersion` in state file ≠ currently-running kit. Recommend `agentic update`.

## Step 3 — Prioritize next actions

Rank findings by leverage. Return 3–5 concrete invocations, each as a one-line "do X next" with the slug or path that makes the action unambiguous.

Priority heuristic:

1. **Decisions blocking work.** Proposed ADRs awaiting acceptance, accepted specs without tasks. These unblock everyone downstream.
2. **Quality gates the profile expects.** `mature` profile + hooks not wired → recommend `/agentic-hooks`. `team` profile + hooks not wired → recommend.
3. **In-flight work needing review.** Branch ahead of `main` without a fresh-context review → recommend `/agentic-review`.
4. **Drift / hygiene.** Orphan tasks, state-file staleness, spec ↔ task gaps → recommend `/agentic-audit` or `agentic update`.
5. **Greenfield gaps.** Missing `AGENTS.md` → `/agentic-bootstrap`. Missing `ARCHITECTURE.md` → `/agentic-architecture` (skip in `poc` and `solo`).

If nothing actionable surfaces, say so explicitly — empty output is real signal, not a gap. Phrase: "No urgent next action. Continue current work or invoke `/agentic-audit` for a full drift check."

## Step 4 — Profile-aware filtering

Apply per-profile rules at the end so the user sees output matched to their maturity:

- **`poc`:** suppress Layer 2 (specs) and Layer 3 (ADRs / tasks) sections entirely if those directories do not exist. Show Layer 1 + Layer 4 only. Recommendation set: `/agentic-ground` for research, `/agentic-audit` for drift, `/agentic-update` for staleness.
- **`solo`:** Layer 2 / Layer 3 render but ADR / `ARCHITECTURE.md` absence is informational — no "needs action" flag. Specs are universal at this profile; spec-without-tasks remains a real finding.
- **`team`:** full survey. Default profile.
- **`mature`:** additionally flag hooks-not-wired louder ("WORKFLOW §11 binding for `mature` profile — `/agentic-hooks` recommended").

## Output contract

A single Markdown message structured as:

```
## agentic-next

**Profile:** <name> (kit v<X.Y.Z>)
**Branch:** <name> (<n> commits ahead of main)

### Layer 1 — Constitution
<one-line status per artifact>

### Layer 2 — Specs (doc/specs/)
<spec list with status + task count, or "no specs">

### Layer 3 — Plans / Decisions
<ADR + task summaries with explicit flags>

### Layer 4 — Code
<branch / tests / hooks / CI status>

### Recommended next (priority)
1. <action> — <one-line reason>
2. <action> — <one-line reason>
...
```

No file written. No state mutation. Recommendations are advisory; the user decides whether to invoke. Cross-references `agentic-audit` (drift detection), `agentic-update` (kit drift), `agentic-profile` (profile changes) where they apply.

When the host exposes `AskUserQuestion` (per ADR-0014) and the user follows up with a confirmation question after seeing the recommendations, prefer the structured prompt over inline text.
