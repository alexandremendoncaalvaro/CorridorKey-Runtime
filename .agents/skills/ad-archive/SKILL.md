---
name: ad-archive
description: Sweep completed plan files (tasks Status:done, specs Status:shipped, PRDs Status:superseded, ADRs Status:superseded or deprecated) out of the working tree and into git history via `git rm`. Use when the user wants to archive, clean, prune, or sweep done tasks / shipped specs / superseded PRDs / superseded or deprecated ADRs / completed legacy plan documents. Hard-delete-only — git history is the ledger, no in-tree archive subdirectory, no `CHANGELOG.md` (Rule #2 forbids changelogs in narrative documents). ADRs with `Status: accepted` are removable only when the user names them and the substance is verifiably absorbed into a binding doc (ARCHITECTURE.md / GUIDELINES.md / AGENTS.md / code).
summary: Hard-delete completed plan files (tasks / specs / PRDs / superseded ADRs) into git history. ADR-accepted requires absorption proof.
---

<background_information>
Removes plan files whose decision-record lifecycle is over. Git history retains the content; the working tree stops paying context cost on artifacts the LLM no longer needs to scan.

Three rules anchor the design:
- No `CHANGELOG.md`, no `archive/` subdirectory. WORKFLOW.md Rule #2 forbids changelogs in narrative documents; an `archive/` subdir still expands under directory globs. Git history is the ledger; `/ad-commit` after `/ad-archive` is the breadcrumb. `git log --diff-filter=D -- doc/adr/` reaches it when needed.
- Status-driven, not age-driven. Accepted means binding regardless of age.
- Accepted ADRs require absorption proof. Nygard's tradition treats accepted ADRs as immutable; the kit's lean-tree value justifies removal only when the ADR's substance — not just its slug citation — has been moved into a binding document. The skill enforces this by grepping the named target for the ADR's core claim before allowing `git rm`.
</background_information>

<instructions>
Step 1 — Discover candidates. Read frontmatter only; do not load full bodies.

Tasks (`doc/tasks/NNNN-*.md`): include if `Status: done`. Exclude `proposed`, `in-progress`, `blocked`.

Specs (`doc/specs/NNNN-*.md`): include if `Status: shipped`. Exclude `draft`, `accepted`, anything `superseded by SPEC-NNNN` (chain target stays).

PRDs (`doc/product/PRD.md` single-product or `doc/product/<slug>.md` multi-product): include if `Status: superseded`. Exclude `draft`, `accepted`.

ADRs (`doc/adr/NNNN-*.md`):
- Auto-include if `Status: superseded by ADR-NNNN` or `Status: deprecated`. Chain target stays.
- Do NOT auto-include `Status: accepted`. Accepted ADRs require the explicit Step 3 absorption check.

Legacy plan docs (prose files under `doc/` not in `adr/`/`tasks/`/`specs/`/`product/`; e.g. `doc/v0.2-cli-plan.md`): present as candidates only when the user explicitly names them. No `Status:` field — user judgement required.

Step 2 — Present the slate. One block per category, one line per candidate, format:

```
### Tasks (done) — N candidates
- doc/tasks/NNNN-slug.md   done   <created date>   "<title>"

### Specs (shipped) — N candidates
(none)

### PRDs (superseded) — N candidates
(none)

### ADRs (superseded / deprecated) — N candidates
- doc/adr/NNNN-slug.md   superseded by ADR-MMMM   <date>
```

Print a one-line summary tail: `Total: N candidates across M categories. Hard-delete via git rm; git history retains. No CHANGELOG or archive subdir.`

If a category has zero candidates: render heading and `(none)` — do not skip silently.

If user named legacy plan docs: render under `### Legacy plan docs (user-named) — N candidates`.

Step 3 — Confirm + accepted-ADR absorption gate.

Ask the user which categories to sweep. Default phrasing: "Proceed with all auto-included candidates? Or name a subset (e.g. 'tasks only', 'tasks + specs', 'skip ADR-0019')."

If the user adds any `Status: accepted` ADR to the removal set, run the absorption check before accepting:

1. Ask the user where the decision has been absorbed. Required form: `<file>#<section-or-anchor>` or `<file>:<line>` (e.g. `ARCHITECTURE.md#data-flow`, `GUIDELINES.md§Naming`, `src/lib/profiles.js:23`).
2. Extract two-to-four substance keywords from the ADR's title and Decision section (not the slug — substance). Example: ADR-0004 "File-based task tracking" → keywords `file-based`, `task tracking`, `doc/tasks`, `Markdown`.
3. Grep the absorption target for each keyword. Require at least one keyword match.
4. If grep fails: refuse removal. Print:
   "ADR-NNNN absorption not verified. Substance keyword `<X>` not found in `<target>`. Absorb the rationale first (edit the binding doc), then re-run /ad-archive and re-name this ADR."
5. If grep succeeds: add to removal set, print matched location as evidence.

Do not silently downgrade the check. The absorption rule is the design's load-bearing constraint.

Step 4 — Execute. Single `git rm` invocation for all confirmed files (one shell call, atomic stage):

```bash
git rm doc/tasks/0001-...md doc/tasks/0002-...md doc/adr/0019-...md
```

Then run `git status --short` and print the staged deletions. Do not commit. Do not push. User runs `/ad-commit` next.

If `git rm` fails on any file: abort the entire batch, print error verbatim, instruct user to resolve before re-running.

Step 5 — Hand off. Print exactly:

```
N file(s) staged for deletion. Git history retains content.
Discoverability: `git log --diff-filter=D --name-only -- doc/`

Next: /ad-commit
Suggested subject: `docs: archive <N> implemented <kind(s)>`
Suggested body: list removed slugs and the binding doc(s) the substance was absorbed into (for accepted-ADR removals).
```
</instructions>

<output_contract>
Writes nothing to disk other than `git rm` staging. Never auto-commits. Refuses to remove `Status: proposed | in-progress | blocked | draft | accepted` artifacts except when the user explicitly names an accepted ADR and the absorption check passes. Refuses to create `CHANGELOG.md` or `doc/<kind>/archive/` subdirectories — both violate kit discipline (Rule #2 and the lean-tree value).
</output_contract>

## Next

- `/ad-commit` — author the deletion commit (Conventional Commits + DCO sign-off).
- `/ad-next` — re-run state survey; removed artifacts disappear from Layer 4 / Layer 5 counts.
- `/ad-audit` — confirm no narrative document still cites removed ADRs by slug as load-bearing context.
