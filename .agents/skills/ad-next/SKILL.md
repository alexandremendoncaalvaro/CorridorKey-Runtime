---
name: ad-next
description: Survey the project's state across the six-layer artifact stack and recommend prioritized next actions, modeled on `flutter doctor`. Use when the user asks "what's next", "next step", "where am I", "project status", "doctor", "what should I do", "audit my workflow". Read-only; complements `ad-drift` (drift detection, a different question). Profile-aware — `poc` suppresses Layer 3/4/5 noise, `team`/`mature` run the full survey.
summary: State survey + prioritized next-action recommendations across the six-layer artifact stack. Read-only navigation aid (`flutter doctor` pattern).
---

<background_information>
Read-only state survey + prioritized next-action recommendations. Mirrors `flutter doctor` shape: layer-by-layer status + concrete fix per finding. Complements `ad-drift` — audit answers "is anything wrong?", next answers "what should I do?".

The skill writes nothing. Output is recommendations the user copies into the next conversation turn or the next CLI invocation.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user asks about workflow status, invoke this skill manually.
</background_information>

<instructions>
Step 0 — read state. Detect baseline:
- Profile + kit version: read `.claude/agentic-state.json` and `.agents/agentic-state.json` if present. Profile defaults to `team` per ADR-0013 when state file missing or no profile field.
- Filesystem signals: `AGENTS.md` / `CLAUDE.md`, `GUIDELINES.md`, `ARCHITECTURE.md`, `DESIGN.md`, `WORKFLOW.md`, `README.md`, `package.json` / `pyproject.toml` / `Cargo.toml` / `go.mod`, `.husky/` / `lefthook.yml` / `.pre-commit-config.yaml`, `.github/workflows/`, current git branch.
- Meaningful-code signals: non-trivial files under `src/`, `app/`, `lib/`, `test/`, `tests/`, `packages/`, framework entrypoints, or manifests with real scripts/dependencies. Treat only README/LICENSE/gitignore files, agentic state, empty artifact directories, and empty manifests as trivial.
- Durable product-framing signals: PRD, specs, tasks, or README/docs/code that let you summarize the target user, problem, and current product behavior. Framework scaffolds or a few early files without that framing still count as unframed greenfield.
- Per-artifact directories: list `doc/product/`, `doc/specs/`, `doc/adr/`, `doc/tasks/`. Read each file's frontmatter (`Status:`, `Created:`, `Spec ref:` for tasks) but NOT the full body.
- Git state: current branch, commits ahead of `main` (`git rev-list --count main..HEAD`), unpushed commits, working-tree dirtiness.
- Root-doc freshness: inspect headings and references only. If a PRD exists but `AGENTS.md` / `CLAUDE.md` does not reference `doc/product/` / `PRD`, mark the operational guide as possibly stale and recommend refreshing via `/ad-bootstrap` after the product contract.

Do not parse skill bodies. Do not run tests. Do not invoke other skills.

Step 1 — classify scenario before ranking. Layer status is evidence; scenario determines the right next step.

- Fresh / unframed greenfield: no durable product framing, even if a framework scaffold or a few early files already exist. This is a product-framing problem, not an `AGENTS.md` problem.
- Product-framed greenfield: PRD exists, but no meaningful code yet. This is where `/ad-bootstrap`, `/ad-guidelines`, optional `/ad-design`, then `/ad-spec` become the normal sequence.
- Brownfield: meaningful code exists and the scan can summarize the current product behavior. Existing code can supply product and architecture evidence; `/ad-bootstrap` is scan-first here and may precede PRD backfill.
- Feature planning: PRD/spec artifacts exist and have downstream gaps (accepted PRD with no specs, accepted spec with no tasks).
- Implementation in progress: dirty tree, branch ahead of `main`, in-progress tasks, blocked tasks, or proposed ADRs.
- Maintenance / install hygiene: stale kit state, profile/install mismatch, missing expected conditional skills.

If scenarios overlap, report the strongest active scenario in this order: implementation in progress, maintenance/install hygiene, feature planning, product-framed greenfield, brownfield, fresh/unframed greenfield. If code exists but product behavior cannot be summarized, choose fresh/unframed greenfield rather than brownfield.

Step 2 — layer-by-layer status. Render six sections in this exact order. Use words for status (`present`, `in flight`, `missing`, `stale`) — no emoji.

Layer 1 — Constitution: `WORKFLOW.md` (kit-shipped), `AGENTS.md` / `CLAUDE.md` (operational guide), `GUIDELINES.md` (full engineering reference). `AGENTS.md` missing is not the first greenfield finding when product framing is missing; recommend product discovery / PRD first, then `/ad-bootstrap`.

Layer 2 — Domain (CONTEXT.md): present at repo root, *or* CONTEXT-MAP.md plus per-context CONTEXT.md files? Lazy-created per ADR-0019 — `missing` is valid for projects whose first domain term has not been resolved yet, not a finding to flag in poc / solo. For each present file, flag empty-glossary (Language section with zero terms).

Layer 3 — Product (doc/product/): doc/product/PRD.md (single-product) or PRODUCT-MAP.md plus per-product slug files (multi-product)? Lazy-created per ADR-0027 — `missing` is valid at `poc` (PRD profile-excluded). In fresh/unframed greenfield at solo/team/mature, missing PRD is the primary navigation finding. For each present PRD, report Status + count of feature specs whose Related → PRD field points at it. Flag accepted PRDs with zero implementing specs.

Layer 4 — Specs (doc/specs/): for each file, report Status + count of tasks whose Spec ref points at it. Flag specs with Status: accepted and zero implementing tasks. If frontend signals exist, also report `DESIGN.md` as the visual contract; missing `DESIGN.md` is a recommendation before `/ad-spec` only when frontend tokens/styles exist or the next feature touches UI.

Layer 5 — Plans / Decisions: `ARCHITECTURE.md`, doc/adr/ counts by status, doc/tasks/ counts by status. Flag proposed ADRs with their slug. List in-progress + blocked tasks with slug and Spec ref. Flag tasks with no Spec ref and no Board ref as orphans. `ARCHITECTURE.md` missing is a finding for team/mature brownfield or when a spec creates load-bearing system patterns; it is not the first step in fresh greenfield.

Layer 6 — Code: branch + ahead count, tests wired? (npm test / pytest / cargo test / go test), hooks wired? (.husky / lefthook.yml / .pre-commit-config.yaml / .git/hooks/), CI wired? (.github/workflows / .gitlab-ci.yml / .circleci/).

Step 3 — cross-cut signals:
- Pending fresh-context review: branch ≥1 commits ahead of main with no .agentic/reviews/<ts>-*.md for the current range → recommend ad-review.
- Spec ↔ task reciprocity: tasks with non-empty Spec ref whose target spec is missing → orphan; accepted/shipped specs with zero Related → Tasks → spec without implementing tasks.
- Profile vs install state: profile-declared skill set ≠ on-disk skill set → recommend `agentic update` or `agentic profile set <name>`.
- Stale state file: kitVersion in state file ≠ currently-running kit → recommend `agentic update`.

Step 4 — prioritize next actions. Rank by workflow leverage, not by document layer number. Return 3–5 concrete invocations, each as one-line "do X next" with slug / path.

Priority heuristic:
1. Protect active work: blocked tasks, proposed ADRs blocking implementation, dirty/ahead branch needing `/ad-review`, stale state that makes installed skills unreliable.
2. Fresh / unframed greenfield: for solo/team/mature, recommend `/ad-grill-me` when the product ask is fuzzy or `/ad-prd` when it is clear; then `/ad-bootstrap`. Do not recommend `/ad-bootstrap` first, even when a framework scaffold already exists.
3. Product-framed greenfield: if PRD exists, recommend `/ad-bootstrap` when `AGENTS.md` / `CLAUDE.md` is missing or stale, then `/ad-guidelines`, optional `/ad-design`, then `/ad-spec`.
4. Brownfield: if meaningful code exists and `AGENTS.md` / `CLAUDE.md` is missing, recommend `/ad-bootstrap` scan-first. Then recommend `/ad-guidelines` for standards, `/ad-architecture` for team/mature system patterns, or `/ad-prd` only when product scope is being backfilled or changed.
5. Feature pipeline gaps: accepted PRD without specs → `/ad-spec`; accepted spec without tasks → `/ad-task`; missing research before implementation → `/ad-ground`.
6. Quality gates and drift: mature hooks missing → `/ad-hooks`; orphan tasks/spec mismatches → `/ad-drift`; kit/profile drift → `agentic update` or `agentic profile set <name>`.

If nothing actionable surfaces, say so: "No urgent next action. Continue current work or invoke `/ad-drift` for a full drift check."

Step 5 — profile-aware filtering. Apply at the end:
- poc: suppress Layer 3 (Product), Layer 4 (Specs), Layer 5 (Plans/Decisions) sections if those directories do not exist. Show Layer 1 + Layer 2 + Layer 6 only. Layer 2 (Domain) and Layer 3 (Product) render informationally — CONTEXT.md and PRD.md missing are *not* findings (lazy-created; PRD also profile-excluded). Recommendation set: `/ad-grill-me` for fuzzy exploration, `/ad-ground` for research-ready questions, `/ad-spike` when the technique is uncertain, `/ad-drift` for drift, `agentic update` for staleness. Do not recommend `/ad-prd`, `/ad-spec`, `/ad-task`, `/ad-bootstrap`, `/ad-guidelines`, `/ad-architecture`, `/ad-adr`, or `/ad-hooks` unless the user is graduating the project out of poc.
- solo: Layer 3/4/5 render; ADR / ARCHITECTURE.md absence is informational, not a flag. PRD is universal for real products, but fresh greenfield still starts with product framing before `/ad-bootstrap`; brownfield quick fixes do not need PRD backfill before the fix. Specs are universal; spec-without-tasks remains a real finding. Layer 2 — same lazy-creation rule as poc.
- team: full survey (default). Fresh greenfield still routes through product discovery / PRD before `/ad-bootstrap`; brownfield may bootstrap scan-first from existing code.
- mature: additionally flag hooks-not-wired louder (WORKFLOW §11 binding for mature profile). Keep `/ad-hooks` after product/operational context unless the only finding is missing gates.
</instructions>

<output_contract>
A single Markdown message structured as:

```
## ad-next

**Profile:** <name> (kit v<X.Y.Z>)
**Branch:** <name> (<n> commits ahead of main)
**Scenario:** <detected scenario>

### Layer 1 — Constitution
<one-line status per artifact>

### Layer 2 — Domain (CONTEXT.md)
<present / lazy-missing; glossary-empty flag if file exists but has no terms>

### Layer 3 — Product (doc/product/)
<present / lazy-missing; PRD status + implementing-spec count if file exists>

### Layer 4 — Specs (doc/specs/)
<spec list with status + task count, or "no specs"; DESIGN.md status when frontend signals exist>

### Layer 5 — Plans / Decisions
<ADR + task summaries with explicit flags>

### Layer 6 — Code
<branch / tests / hooks / CI status>

### Recommended next (priority)
1. <action> — <one-line reason>
2. <action> — <one-line reason>
```

No file written. No state mutation. Recommendations are advisory; the user decides whether to invoke. Cross-references `ad-drift` (drift detection), `agentic update` (kit drift — CLI subcommand, not a skill), `agentic profile` (profile changes — CLI subcommand, not a skill) where they apply.
</output_contract>
