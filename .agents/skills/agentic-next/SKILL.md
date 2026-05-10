---
name: agentic-next
description: Survey the project's state across the four-layer artifact stack and recommend prioritized next actions, modeled on `flutter doctor`. Use when the user asks "what's next", "next step", "where am I", "project status", "doctor", "what should I do", "audit my workflow". Read-only; complements `agentic-audit` (drift detection, a different question). Profile-aware — `poc` suppresses Layer 2/3 noise, `team`/`mature` run the full survey.
---

<background_information>
Read-only state survey + prioritized next-action recommendations. Mirrors `flutter doctor` shape: layer-by-layer status + concrete fix per finding. Complements `agentic-audit` — audit answers "is anything wrong?", next answers "what should I do?".

The skill writes nothing. Output is recommendations the user copies into the next conversation turn or the next CLI invocation.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user asks about workflow status, invoke this skill manually.
</background_information>

<instructions>
Step 0 — read state. Detect baseline:
- Profile + kit version: read `.claude/agentic-state.json` and `.agents/agentic-state.json` if present. Profile defaults to `team` per ADR-0013 when state file missing or no profile field.
- Filesystem signals: `AGENTS.md` / `CLAUDE.md`, `ARCHITECTURE.md`, `DESIGN.md`, `WORKFLOW.md`, `README.md`, `package.json` / `pyproject.toml` / `Cargo.toml` / `go.mod`, `.husky/` / `lefthook.yml` / `.pre-commit-config.yaml`, `.github/workflows/`, current git branch.
- Per-artifact directories: list `doc/specs/`, `doc/adr/`, `doc/tasks/`. Read each file's frontmatter (`Status:`, `Created:`, `Spec ref:` for tasks) but NOT the full body.
- Git state: current branch, commits ahead of `main` (`git rev-list --count main..HEAD`), unpushed commits, working-tree dirtiness.

Do not parse skill bodies. Do not run tests. Do not invoke other skills.

Step 1 — layer-by-layer status. Render four sections in this exact order. Use words for status (`present`, `in flight`, `missing`, `stale`) — no emoji.

Layer 1 — Constitution: AGENTS.md / CLAUDE.md, WORKFLOW.md, ARCHITECTURE.md, DESIGN.md (frontend only).

Layer 2 — Specs (doc/specs/): for each file, report Status + count of tasks whose Spec ref points at it. Flag specs with Status: accepted and zero implementing tasks.

Layer 3 — Plans / Decisions: doc/adr/ counts by status, flag proposed ADRs with their slug. doc/tasks/ counts by status, list in-progress + blocked with slug and Spec ref. Flag tasks with no Spec ref and no Board ref as orphans.

Layer 4 — Code: branch + ahead count, tests wired? (npm test / pytest / cargo test / go test), hooks wired? (.husky / lefthook.yml / .pre-commit-config.yaml / .git/hooks/), CI wired? (.github/workflows / .gitlab-ci.yml / .circleci/).

Step 2 — cross-cut signals:
- Pending fresh-context review: branch ≥1 commits ahead of main with no .agentic/reviews/<ts>-*.md for the current range → recommend agentic-review.
- Spec ↔ task reciprocity: tasks with non-empty Spec ref whose target spec is missing → orphan; accepted/shipped specs with zero Related → Tasks → spec without implementing tasks.
- Profile vs install state: profile-declared skill set ≠ on-disk skill set → recommend `agentic update` or `agentic profile set <name>`.
- Stale state file: kitVersion in state file ≠ currently-running kit → recommend `agentic update`.

Step 3 — prioritize next actions. Rank by leverage. Return 3–5 concrete invocations, each as one-line "do X next" with slug / path.

Priority heuristic:
1. Decisions blocking work (proposed ADRs, accepted specs without tasks).
2. Quality gates the profile expects (mature + hooks not wired).
3. In-flight work needing review (branch ahead without fresh-context review).
4. Drift / hygiene (orphan tasks, state-file staleness, spec ↔ task gaps).
5. Greenfield gaps (missing AGENTS.md, missing ARCHITECTURE.md — skip in poc / solo).

If nothing actionable surfaces, say so: "No urgent next action. Continue current work or invoke `/agentic-audit` for a full drift check."

Step 4 — profile-aware filtering. Apply at the end:
- poc: suppress Layer 2/3 sections if those directories do not exist. Show Layer 1 + Layer 4 only. Recommendation set: `/agentic-ground`, `/agentic-audit`, `agentic update`.
- solo: Layer 2/3 render; ADR / ARCHITECTURE.md absence is informational, not a flag. Specs are universal; spec-without-tasks remains a real finding.
- team: full survey (default).
- mature: additionally flag hooks-not-wired louder (WORKFLOW §11 binding for mature profile).
</instructions>

<output_contract>
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
```

No file written. No state mutation. Recommendations are advisory; the user decides whether to invoke. Cross-references `agentic-audit` (drift detection), `agentic-update` (kit drift), `agentic-profile` (profile changes) where they apply.
</output_contract>
