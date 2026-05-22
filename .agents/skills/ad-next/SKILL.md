---
name: ad-next
description: Survey the project's state across the six-layer artifact stack and recommend prioritized next actions, modeled on `flutter doctor`. Use when the user asks "what's next", "next step", "where am I", "project status", "doctor", "what should I do", "audit my workflow". Read-only; complements `ad-audit` (drift detection, a different question). Profile-aware — `poc` suppresses Layer 3/4/5 noise, `team`/`mature` run the full survey.
summary: State survey + prioritized next-action recommendations across the six-layer artifact stack. Read-only navigation aid (`flutter doctor` pattern).
---

<background_information>
Read-only state survey + prioritized next-action recommendations. Mirrors `flutter doctor` shape: layer-by-layer status + concrete fix per finding. Complements `ad-audit` — audit answers "is anything wrong?", next answers "what should I do?".

The skill writes nothing. Output is recommendations the user copies into the next conversation turn or the next CLI invocation.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user asks about workflow status, invoke this skill manually.
</background_information>

<instructions>
Step 0 — read state. Detect baseline:
- Profile + kit version: read `.claude/agentic-state.json` and `.agents/agentic-state.json` if present. Profile defaults to `team` per ADR-0013 when state file missing or no profile field.
- Filesystem signals: `AGENTS.md` / `CLAUDE.md`, `ARCHITECTURE.md`, `DESIGN.md`, `WORKFLOW.md`, `README.md`, `package.json` / `pyproject.toml` / `Cargo.toml` / `go.mod`, `.husky/` / `lefthook.yml` / `.pre-commit-config.yaml`, `.github/workflows/`, current git branch.
- Per-artifact directories: list `doc/product/`, `doc/specs/`, `doc/adr/`, `doc/tasks/`. Read each file's frontmatter (`Status:`, `Created:`, `Spec ref:` for tasks) but NOT the full body.
- Git state: current branch, commits ahead of `main` (`git rev-list --count main..HEAD`), unpushed commits, working-tree dirtiness.

Do not parse skill bodies. Do not run tests. Do not invoke other skills.

Step 1 — layer-by-layer status. Render six sections in this exact order. Use words for status (`present`, `in flight`, `missing`, `stale`) — no emoji.

Layer 1 — Constitution: AGENTS.md / CLAUDE.md, WORKFLOW.md, ARCHITECTURE.md, DESIGN.md (frontend only).

Layer 2 — Domain (CONTEXT.md): present at repo root, *or* CONTEXT-MAP.md plus per-context CONTEXT.md files? Lazy-created per ADR-0019 — `missing` is valid for projects whose first domain term has not been resolved yet, not a finding to flag in poc / solo. For each present file, flag empty-glossary (Language section with zero terms).

Layer 3 — Product (doc/product/): doc/product/PRD.md (single-product) or PRODUCT-MAP.md plus per-product slug files (multi-product)? Lazy-created per ADR-0027 — `missing` is valid at `poc` (PRD profile-excluded). For each present PRD, report Status + count of feature specs whose Related → PRD field points at it. Flag accepted PRDs with zero implementing specs.

Layer 4 — Specs (doc/specs/): for each file, report Status + count of tasks whose Spec ref points at it. Flag specs with Status: accepted and zero implementing tasks.

Layer 5 — Plans / Decisions: doc/adr/ counts by status, flag proposed ADRs with their slug. doc/tasks/ counts by status, list in-progress + blocked with slug and Spec ref. Flag tasks with no Spec ref and no Board ref as orphans.

Layer 6 — Code: branch + ahead count, tests wired? (npm test / pytest / cargo test / go test), hooks wired? (.husky / lefthook.yml / .pre-commit-config.yaml / .git/hooks/), CI wired? (.github/workflows / .gitlab-ci.yml / .circleci/).

Step 2 — cross-cut signals:
- Pending fresh-context review: branch ≥1 commits ahead of main with no .agentic/reviews/<ts>-*.md for the current range → recommend ad-review.
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

If nothing actionable surfaces, say so: "No urgent next action. Continue current work or invoke `/ad-audit` for a full drift check."

Step 4 — profile-aware filtering. Apply at the end:
- poc: suppress Layer 3 (Product), Layer 4 (Specs), Layer 5 (Plans/Decisions) sections if those directories do not exist. Show Layer 1 + Layer 2 + Layer 6 only. Layer 2 (Domain) and Layer 3 (Product) render informationally — CONTEXT.md and PRD.md missing are *not* findings (lazy-created; PRD also profile-excluded). Recommendation set: `/ad-ground`, `/ad-audit`, `agentic update`.
- solo: Layer 3/4/5 render; ADR / ARCHITECTURE.md absence is informational, not a flag. PRD is universal; PRD-without-specs is a real finding. Specs are universal; spec-without-tasks remains a real finding. Layer 2 — same lazy-creation rule as poc.
- team: full survey (default).
- mature: additionally flag hooks-not-wired louder (WORKFLOW §11 binding for mature profile).
</instructions>

<output_contract>
A single Markdown message structured as:

```
## ad-next

**Profile:** <name> (kit v<X.Y.Z>)
**Branch:** <name> (<n> commits ahead of main)

### Layer 1 — Constitution
<one-line status per artifact>

### Layer 2 — Domain (CONTEXT.md)
<present / lazy-missing; glossary-empty flag if file exists but has no terms>

### Layer 3 — Product (doc/product/)
<present / lazy-missing; PRD status + implementing-spec count if file exists>

### Layer 4 — Specs (doc/specs/)
<spec list with status + task count, or "no specs">

### Layer 5 — Plans / Decisions
<ADR + task summaries with explicit flags>

### Layer 6 — Code
<branch / tests / hooks / CI status>

### Recommended next (priority)
1. <action> — <one-line reason>
2. <action> — <one-line reason>
```

No file written. No state mutation. Recommendations are advisory; the user decides whether to invoke. Cross-references `ad-audit` (drift detection), `agentic update` (kit drift — CLI subcommand, not a skill), `agentic profile` (profile changes — CLI subcommand, not a skill) where they apply.
</output_contract>
