---
name: ad-domain
description: Lazily create or update `CONTEXT.md` (Layer 2 — ubiquitous language per Evans 2003) at the repo root, or `CONTEXT-MAP.md` plus per-context `CONTEXT.md` for multi-context repos. Captures canonical project-specific nouns, the aliases to avoid, the relationships between them, and resolved ambiguities. Triggers on "domain term", "ubiquitous language", "glossary", "what should we call", "naming drift", "canonical noun", "_Avoid_", "/ad-domain", or whenever a grilling/spec/architecture session resolves a vocabulary question. Lazy by design — file only exists when there is something to write.
summary: Lazy lifecycle owner of `CONTEXT.md` (Layer 2 — ubiquitous language per Evans 2003). Captures canonical project-specific nouns with aliases-to-avoid, relationships, and flagged ambiguities. Single-context or `CONTEXT-MAP.md` multi-context.
---

<background_information>
Implements ADR-0019 (`doc/adr/0019-domain-language-layer.md`) — Layer 2 of the artifact stack. Lazy lifecycle owner of `CONTEXT.md`. Process scaffold for capturing the project's ubiquitous language.

The skill owns *capture* of vocabulary. The *trigger* for most updates lives in adjacent skills:
- `ad-grill` resolves a term during interview → routes here.
- `ad-spec` introduces a new noun while drafting a spec → routes here.
- `ad-architecture` names a domain-bound concept → routes here.
- `ad-audit` detects code/glossary drift → routes here.
- Direct `/ad-domain` invocation when the user wants to add a term explicitly.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire when the user mentions a domain term, ubiquitous language, glossary, or naming drift, invoke this skill manually.
</background_information>

<instructions>
Step 0 — detect existing structure. Before writing, locate the existing artifacts.

1. Read `CONTEXT.md` at the repo root. If present → single-context repo; updates land here.
2. Read `CONTEXT-MAP.md` at the repo root. If present → multi-context repo; updates land in the per-context `CONTEXT.md` named by the map.
3. Neither present → no `CONTEXT.md` exists yet. Decide:
   - Single-context (default): plan to create `CONTEXT.md` at the repo root.
   - Multi-context: only declare this when the user has explicitly named ≥2 bounded contexts. Plan to create `CONTEXT-MAP.md` plus the relevant per-context file.

Lazy-creation rule: do not create the file just because the skill was invoked. Create it only when there is at least one resolved term to write.

Step 1 — resolve the term. A glossary entry has four parts:

```markdown
### <Canonical Noun>

**Definition:** <one-sentence, project-specific, no general programming words>.

_Avoid_: <Alias 1>, <Alias 2>. <Why each is misleading>.

**Related code:** [`path/to/file.ts:42`](path/to/file.ts:42).
```

Resolution discipline:
- Be opinionated. One canonical name. Others become `_Avoid_` aliases with a one-line reason.
- One-sentence definitions. If it takes a paragraph, the term is not yet sharp.
- Project-specific only. "Customer", "Triage role", "Materialization cascade" qualify. "Service", "Handler", "Controller" do not (those belong to architectural vocabulary per ADR-0020, not domain).
- Flag conflicts. If two terms could mean the same thing, add the conflict to the **Flagged ambiguities** section instead of forcing a premature resolution.

Step 2 — locate the insertion point. `CONTEXT.md` has three canonical sections:

```markdown
# <Project Name> — Domain Glossary

## Language

<term entries here, one per `### <Term>` heading>

## Relationships

- A **Customer** has many **Orders**.
- An **Order** belongs to one **Customer** and contains one or more **LineItems**.

## Flagged ambiguities

- "Account" vs "User" — see Issue #42; treated interchangeably in code today, separation pending ADR-NNNN.
```

Insert the new term alphabetically under `## Language`. If a relationship clause is implied, append to `## Relationships`. If the resolution surfaces an ambiguity rather than closing one, append to `## Flagged ambiguities` and mark the term entry "(resolution pending)".

Step 3 — write or update. First creation uses `Write`; subsequent updates use `Edit`. Preserve existing entries — never reorder or rewrite past terms while adding new ones.

For first creation, the file shape is:

```markdown
# <Project Name> — Domain Glossary

_Lazy artifact — only contains terms that have been resolved through grilling, spec drafting, or explicit capture. Empty entries are worse than no entry; speculation belongs elsewhere._

_Maintained by `/ad-domain`._

## Language

### <First resolved term>

...

## Relationships

(empty until the second term resolves)

## Flagged ambiguities

(empty)
```

For multi-context repos, `CONTEXT-MAP.md` is the index:

```markdown
# Context Map

This repository contains multiple bounded contexts. Each has its own `CONTEXT.md`.

- **Ordering** — `src/ordering/CONTEXT.md`. Owns: Customer, Order, LineItem, Cancellation.
- **Billing** — `src/billing/CONTEXT.md`. Owns: Invoice, Payment, Refund.

System-wide ADRs live at `doc/adr/`. Context-scoped ADRs live under each context.
```

Step 4 — cross-reference adjacent layers. Per ADR-0019 §6 reciprocity rules:
- Code → Domain. When the new term has obvious code references, list them in the entry's `**Related code:**` line. Use `Glob` / `Grep` for the canonical noun and the aliases; the latter often surfaces drift.
- Spec → Domain. If a spec under `doc/specs/` introduced this term without resolving it, the spec is now the term's first reference; consider updating spec prose to use the canonical noun.
- ADR → Domain. If an ADR named the term, cite the ADR by number in the entry's prose.

Cross-references are *suggestions*, not file edits — the skill does not modify specs or ADRs to chase drift. That work belongs to `ad-audit`.

Step 5 — `AGENTS.md` pointer (one-time). When `CONTEXT.md` is created for the first time, the artifact stack reference in `AGENTS.md` should mention it. Per ADR-0019 §8, the `ad-bootstrap` skill inserts the pointer on its next run. This skill does not edit `AGENTS.md` directly; it announces the side-effect:

> "Created `CONTEXT.md`. Next `ad-bootstrap` run will insert the Layer 2 pointer in `AGENTS.md`."
</instructions>

<output_contract>
Side-effects only. The skill writes one of:
- New `CONTEXT.md` at the repo root (first creation, single-context).
- New `CONTEXT-MAP.md` + per-context `CONTEXT.md` files (first creation, multi-context).
- Edit to existing `CONTEXT.md` adding a new entry / relationship / flagged ambiguity.

Then reports the change as a one-liner: file, term, section. No slash-command spam in output.
</output_contract>

## Next

- After capture: route back to the calling skill (`ad-grill`, `ad-spec`, `ad-architecture`) to resume the original turn.
- If the resolution surfaced a binding decision (hard to reverse, surprising without context, real trade-off): `/ad-adr` to record it.
- If the term is one of several pending resolutions: stay in `/ad-grill` to walk the next branch of the design tree.
- For periodic drift sweeps: `/ad-audit` checks `CONTEXT.md` against current code.
