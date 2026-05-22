---
name: ad-deepen
description: Surface deepening opportunities in the codebase using the Ousterhout/Feathers vocabulary from WORKFLOW §8 (Module / Interface / Depth / Seam / Adapter / Leverage / Locality). Three-phase process — explore organically, present numbered candidates with deletion-test framing, grilling loop on the chosen candidate. Pairs with `ad-audit` (audit detects drift; deepen proposes refactors). Triggers on "deepen", "refactor for depth", "shallow module", "deletion test", "two-adapters rule", "interface is the test surface", "leverage", "locality", "/ad-deepen". Profile-scoped to `team` and `mature` only — premature for `poc` and `solo` per ADR-0020 §4.
summary: Surface deepening opportunities using WORKFLOW §8 vocabulary (Module / Interface / Depth / Seam / Adapter / Leverage / Locality). Three phases — explore, present numbered candidates with deletion-test framing, grill the chosen one. Pairs with `ad-audit`. Profile-scoped to `team` and `mature` only.
---

<background_information>
Implements ADR-0020 (`doc/adr/0020-deep-modules-vocabulary.md`) — operational counterpart to the WORKFLOW §8 architectural vocabulary. Surfaces deepening opportunities and walks the user through them. Process scaffold; no primary file output.

Profile scope: `team` and `mature` only. Architectural deepening is premature for ≤200-line experiments. `poc` and `solo` users do not have this skill installed; to enable deepening, re-init the project at `team` profile (`agentic profile set team`) once the codebase has stabilized enough to warrant it. Per ADR-0020 §4.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions deepening, shallow modules, the deletion test, or the two-adapters rule, invoke this skill manually.
</background_information>

<instructions>
Step 0 — confirm regime. Deepen is for the *stable-codebase, friction-visible* regime. Run when at least one holds:
- A module's interface is roughly as complex as its implementation (shallow module candidate).
- A concept is bouncing between modules across recent changes (locality candidate).
- A test cannot exercise the real bug pattern at the call site (interface-is-test-surface candidate).
- An adapter has been introduced for a hypothetical second implementation (two-adapters-rule violation).
- The user explicitly asks "where should this be refactored?" against a stable surface.

Route elsewhere when:
- The change is a one-line fix or mechanical refactor → no scaffold needed.
- The codebase is `poc`-shaped (one-shot script, ≤200 lines, no callers): deepening is premature; ship the experiment first.
- The friction is a *bug*, not a *shape* — `ad-diagnose` (WORKFLOW §15).
- The friction is a *naming/vocabulary* drift — `ad-domain`.
- The friction is *missing context*, not module shape — `ad-grill`.

Step 1 — explore organically. Before naming candidates, read.

1. Read `CONTEXT.md` if it exists. Anchor domain vocabulary; deepening proposals must use *Customer*, *Order*, *Triage role* — not *Service*, *Handler*, *Manager*.
2. Read `ARCHITECTURE.md` if it exists. Read accepted ADRs in `doc/adr/` covering the surface you are about to walk.
3. Walk the codebase noting *friction*:
   - Concepts bouncing between modules (locality fault).
   - Interfaces as wide as their implementations (shallow modules — apply the deletion test).
   - Tests that mock four things to exercise one path (test-surface fault).
   - Adapters with one concrete implementation (two-adapters-rule violation, if no second is planned).

Read with eyes for *shape*, not bugs. The friction is in module geometry; the bug-finding skill is `ad-diagnose`.

Step 2 — present candidates numbered. For each deepening opportunity, produce a short numbered entry:

```markdown
### Candidate N: <one-line title using domain + architectural vocabulary>

**Files involved:** `src/foo.ts:42`, `src/bar.ts`.

**Friction:** <one-paragraph plain English — what hurts today, who feels it, when>.

**Proposal:** <one-paragraph plain English — what changes about the module shape>.

**Vocabulary check:** <Module / Interface / Seam / Adapter / Depth / Leverage / Locality applied here, per WORKFLOW §8>.

**Deletion test:** <if you imagine deleting the module today, what happens? Complexity vanishes → delete instead of refactoring. Complexity reappears across N callers → it earns its keep, deepen it>.

**Two-adapters check:** <if introducing a seam: name the second concrete adapter that justifies it. If only one adapter exists or is planned, the seam is hypothetical — drop it from the proposal>.

**Test surface impact:** <does the new shape make the right pattern testable at the call site? If not, the proposal is wrong-shaped>.

**ADR conflicts:** <name any accepted ADR this proposal contradicts. Flag for reopening only if the friction warrants it; otherwise the candidate is rejected>.
```

Number candidates 1, 2, 3, ... Order by leverage × locality (largest impact first, lowest blast radius among ties). Cap at five.

Step 3 — grilling loop on the chosen candidate. The user picks one candidate by number. Drop into a grilling loop on that one:

1. Walk the design tree of the proposal branch by branch (one question per turn, recommendation included). Reuse the `ad-grill` discipline.
2. Add new domain terms to `CONTEXT.md` lazily via `ad-domain` as the proposal surfaces them.
3. Offer an ADR only when the three criteria pass (hard to reverse, surprising without context, real trade-off —' ' Most refactors do not need an ADR.
4. When the proposal stabilizes: route to `ad-tdg` (WORKFLOW §9) for the implementation pass with ground-truth pair + TDM + criterion-based selection.

Reject candidates the grilling loop reveals as wrong-shaped. Better to discard at this stage than to ship a deepening that doesn't deepen.

Vocabulary discipline (always on). Per ADR-0020, use the canonical vocabulary verbatim:

- Module — any unit with an interface and an implementation.
- Interface — what callers see.
- Implementation — what they don't.
- Depth — behavior leverage at the interface. Deep modules hide a lot of behavior behind a small interface.
- Seam — where behavior can be altered without editing in place (Feathers).
- Adapter — a concrete thing satisfying an interface at a seam. *Role*, not *substance*.
- Leverage — what callers gain from depth.
- Locality — what maintainers gain from depth (concentrated change, bugs, knowledge).

Never use:
- "Boundary" (overloaded with DDD bounded contexts) — use *Seam* or *Interface*.
- "Service" / "Handler" / "Manager" / "Helper" without a domain noun in front.
- "Depth = implementation lines / interface lines" — explicitly rejected by ADR-0020. Rewards padding.

When the candidate's friction touches a domain noun, use the domain noun from `CONTEXT.md` ("the *Order* aggregate"), not a generic placeholder ("the order service").
</instructions>

<output_contract>
Structured conversation. No primary file written. Side-effects:
- `CONTEXT.md` updates land via `ad-domain` when the proposal surfaces new domain terms.
- ADR drafts land via `ad-adr` when the three-criteria test passes.
- Implementation lands via `ad-tdg` after the proposal stabilizes — that skill produces the verified code change.

Each session produces:
1. A numbered list of candidates (Step 2), capped at five.
2. A picked candidate with the grilling loop transcript (Step 3).
3. Routing to `ad-tdg` with the ground-truth pair seeded from the proposal.
</output_contract>

## Next

- After candidates surface but before picking: `/ad-audit` for a broader drift check; the audit may reorder priorities.
- After picking and grilling stabilizes: `/ad-tdg` to implement under WORKFLOW §9 discipline.
- If the proposal touches load-bearing architecture: `/ad-adr` to record the decision (only when the three-criteria test passes).
- If the deepening exposed a domain-vocabulary drift: `/ad-domain` to update `CONTEXT.md`.
- If the candidates all turned out to be bug-symptoms rather than shape-symptoms: `/ad-diagnose` (WORKFLOW §15).
