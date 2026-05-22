---
name: ad-prd
description: Draft or update a Product Requirements Document at doc/product/PRD.md (single-product) or doc/product/<product-slug>.md (multi-product). Layer 3 of the six-layer artifact stack (Constitution → Domain → Product → Spec → Plan/Decisions → Code). Captures product-level scope — target user, problem, goals, non-goals, success metrics, multi-feature roadmap, cross-feature constraints — that feature specs inherit from. Use when the user wants to write, draft, scaffold, or open a product brief, PRD, product requirements, product vision, target user / persona definition, product success metrics, product roadmap, or any product-level (multi-feature) scoping. Distinct from `ad-spec` (feature-level, one feature per spec). Status lifecycle draft → accepted → superseded. Skill is lazy — file only exists when product is being scoped.
summary: Lazy lifecycle owner of `doc/product/PRD.md` (or `doc/product/<slug>.md` multi-product). Layer 3 — product-level scope (target user, problem, success metrics, multi-feature roadmap) that feature specs inherit from. Distinct from `ad-spec` (feature-level).
---

<background_information>
Layer 3 of the artifact stack. Lazy lifecycle owner of the PRD (Product Requirements Document). One product = one PRD; multi-product repos use a `PRODUCT-MAP.md` index plus per-product `<slug>.md` files.

PRD is product-level scope, not feature-level scope. A feature spec (`ad-spec`, Layer 4) implements *part* of a PRD; a PRD is never implemented by a single task. If the user's ask is single-feature, route to `ad-spec` instead.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions PRD, product requirements, target user, product roadmap, or product success metrics, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Run when the user wants to scope or update product-level context: new product framing, existing PRD update, cross-feature constraint surfaced.

Route elsewhere when:

- Scope is one feature → `ad-spec` (Layer 4).
- Scope is a vocabulary question → `ad-domain` (Layer 2).
- Scope is an architectural decision → `ad-adr`.
- Scope is fuzzy and needs interview-before-research → `ad-grill`, which can route back here once the product framing is sharp.

A spike or PoC does not need a PRD — the skill is excluded from the `poc` profile. If the user is on `poc` profile and invokes `ad-prd`, ask explicitly whether the project has graduated to `solo` or `team` before writing.

Step 1 — codebase-first scan. Before asking a single question, look. The repo often answers half the fields.

- Check `doc/product/` — does a PRD already exist?
- Read `AGENTS.md` `## Project Overview` — pulls product name, one-line description, deployment target.
- Read `README.md` head — pulls product framing for users who land via the public README.
- Read `CONTEXT.md` if it exists — anchors the canonical nouns the PRD must use.
- Read `doc/specs/` index — existing feature specs surface the implicit product scope.
- Read `package.json` `name` / `description` / `keywords` for product framing signal.

Only after the scan produces no answer does the skill ask the user. Asking the user about something the repo already states wastes their attention.

Step 2 — determine slug and target file. Single product (default): `doc/product/PRD.md`. Multi-product: ask whether this is a multi-product repo; if yes, file path is `doc/product/<product-slug>.md` and a `doc/product/PRODUCT-MAP.md` index lists each product. Slug: kebab-case, ≤4 words, derived from the product name.

Status starts at `draft`. Created: today, ISO format. Owner: ask once — defaults to the repo's primary committer (from `git config user.name`) if unset.

Step 3 — interview to fill. Ask one question at a time, in this order. Skip questions whose answers are already obvious from the Step 1 scan; surface what the scan found and ask only for confirmation.

- Product. Name and one-sentence positioning. *"X is a Y that does Z for W."*
- Target User. Specific role / persona, not "developers" or "users". Cite the primary success-bearing user; secondary users go under Personas if they affect the product shape.
- Problem. What the target user can't do today, or does badly today. The cost of the status quo. *"What breaks if this product does not exist?"*
- Goals. 3–5 measurable outcomes the product is for. State each as a plain bullet (no checkbox — PRD is definition, not tracking). Pass/fail must be observable; tracking of whether each goal is met lives in per-feature tasks.
- Non-goals. Explicit out-of-scope items readers might assume are in scope. Prevents scope creep without an audit trail.
- Success Metrics. Product-level KPIs — measurable and durable, not procedural. *"Weekly active users above N at 90 days"* — yes. *"Users love it"* — no. Each metric carries the measurement source.
- Roadmap. Multi-feature scope. Each line names a feature, the user value it carries, and the (rough) sequence. Concrete enough to drive `ad-spec` later; not a binding commitment. Mark `MVP`, `Next`, `Later`; no dates.
- Constraints. What binds across the entire product — regulatory, business, technical. Skip if none; do not invent.
- Personas (optional). If the product has secondary users whose needs reshape it. Skip when one persona drives everything.
- Open Questions. Deferred decisions. Each line becomes a future ADR, a spec-time decision, or an explicit punt with rationale.
- Related. ADRs touched, feature specs implementing parts of it, other PRDs this one supersedes. Filled lazily.

Do not invent values. When the user does not know, leave `<TODO>` in the field and continue.

Step 4 — interview UX. Codex has no `AskUserQuestion` primitive. Use inline numbered text for multi-choice prompts. One question per turn; do not chain three text questions in one message.

Step 5 — write the file. Path: `doc/product/PRD.md` (single-product) or `doc/product/<slug>.md` (multi-product). Use the template at `templates/prd.md`. Stop after writing. Do not flip status to `accepted` — that requires user review.

Step 6 — editing guidance for later turns. When the user later works on the PRD:

- Update Success Metrics phrasing only when a metric definition changes; per-metric status tracking lives in per-feature tasks, not in the PRD.
- Append to Open Questions — close them with a resolution paragraph; never delete the original question line.
- Flip `Status` to `accepted` once the user signs off and feature specs start being written against this PRD.
- Flip `Status` to `superseded by <slug>` when a later PRD replaces this one.
- Add Roadmap lines as the product grows; mark superseded roadmap items with a strikethrough and a one-line note, do not delete them.
- Add Related → Specs entries as `ad-spec` runs reference this PRD.

Never rewrite existing prose — append rationale to Open Questions as a resolution paragraph rather than mutating the original requirement text. Append-only Notes discipline.
</instructions>

<output_contract>
- Primary output: `doc/product/PRD.md` or `doc/product/<slug>.md`.
- Multi-product side-effect: `doc/product/PRODUCT-MAP.md` written or updated.
- Status starts `draft`; never flipped to `accepted` by the skill.
- No dates inside narrative prose; the `Created` and `Updated` lifecycle fields are decision-record primitives, exempt from the no-dates rule.

After writing the PRD: route to `ad-spec` for each `MVP` roadmap item. When the PRD surfaces an architectural commitment, route to `ad-adr`. When a roadmap item is technique-uncertain, route to `ad-spike` (WORKFLOW §14). When the PRD framing is fuzzy, route to `ad-grill` to sharpen and return. Periodic drift check: `ad-audit` flags feature specs whose target user or success metrics contradict the PRD.
</output_contract>
