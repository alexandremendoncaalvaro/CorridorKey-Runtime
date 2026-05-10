---
name: agentic-ground
description: Four-source pre-implementation research — official docs, validated open-source examples, in-repo patterns, and git history — then synthesize a happy path and gate any deviation with an irrefutable justification before code is written. Auto-invokes on non-trivial work, refactors, library or pattern selection, "research before coding", "before implementing", "which library", "which pattern", "how to approach", "ground before coding". Workflow-operational counterpart to WORKFLOW.md §4 (Find the Happy Path) and §5 (Ground in Real Patterns); pairs with agentic-philosophy (posture) and agentic-review (post-implementation §10 review).
allowed-tools: Read, Glob, Grep, Bash, WebFetch, WebSearch
---

# /agentic-ground

Implements WORKFLOW §4 + §5 end-to-end as a single research pass. The four sources are joined by **AND**, not OR — every non-trivial change runs the full research pass, then synthesizes a happy path, then justifies any deviation. Output is the input to whatever skill or freeform turn produces the implementation plan; this skill does not write code.

## Step 0 — Scope the research scope

Confirm what is being researched. The research scope is the smallest verifiable surface that captures the change: a function to add, a library to pick, a pattern to apply. State it in one sentence before research starts. If the surface is broader than one sentence captures, ask the user to narrow it; broad research scopes produce diluted research.

If the change is genuinely trivial (rename, typo, one-line refactor on a tested path), skip this skill.

## Step 1 — Four-source research pass (all four required)

### Source A — official documentation

For each language and library in scope, cite the canonical doc URL and version. Use `WebFetch` to confirm the page exists and read the relevant section; use `WebSearch` to locate it if the URL is unknown. If neither produces a confident hit, ask the user for a known-good link rather than fabricating one. Output: bulleted citations, one per language/library, each with URL plus a one-line summary of the relevant guidance.

### Source B — validated open-source examples

Find ≥1 (prefer 2–3) public repository solving the same *technical* research scope with similar techniques. The match is technical, not domain — a CRUD-app-with-auth and a CLI-with-auth both hit "auth flow", and either is valid for the auth research scope. Use `WebSearch` (e.g. `site:github.com <research scope> language:<lang>`) and follow up with `WebFetch` of the specific file. Cite `<repo>:<path>:<line-range>` and quote the relevant block — never paraphrase code from training memory. If search is inconclusive, ask the user for a known reference.

### Source C — in-repo examples

Grep / glob the current repo for analogous patterns. Cite `<file>:<line>` plus a one-line description of how the existing example handles the same shape. If the codebase has no analog, state that explicitly (real signal, not a gap).

### Source D — git history

Run `git log --all --oneline -- <relevant-paths>`, `git log --all --grep=<keyword>`, and a sweep of sibling active branches (`git branch -a`, then `git log <branch> -- <paths>` on those that look related). Surface any prior attempt or sibling solution — including abandoned ones — with `<commit-sha>` plus the touching file path and a one-line description. If the search is genuinely empty, state "no prior attempt found" — that is the valid Source D outcome when there isn't one. Narrow with `--grep` or `-S` on multi-thousand-commit repos.

## Step 2 — Happy path synthesis

In one paragraph, name the most-grounded approach for the research scope and cite at least one source per Source A / B / C. Source D is included when it produced a hit; otherwise mark "no prior attempt found." The paragraph is the canonical answer the engineer would give if asked "what is the canonical, idiomatic way to solve this here?" — the question WORKFLOW.md §4 frames.

## Step 3 — Deviation gate

If the implementation the user (or you) is about to write deviates from the synthesized happy path, write the justification before any code lands. The justification must name the specific constraint, evidence, or trade-off forcing the deviation — generic "we want it differently" is insufficient. If the justification cannot be written confidently, loop back to Step 1 and look harder; do not deviate without it.

The gate is prescriptive, not descriptive: WORKFLOW §4 asks "was the deviation deliberate?"; this gate asks "is the deviation defensible against the four sources?" Write the answer down.

## Step 4 — Confidence checkpoint

Before handing off to implementation, report a soft verdict against four checks:

- A consulted (≥1 official-doc citation per language/library)
- B consulted (≥1 OSS citation, with cite-and-fetched code)
- C consulted (in-repo analog cited or "no analog found" stated)
- D checked (commits / branches surveyed; hit cited or "no prior attempt found")
- Happy path declared (Step 2)
- Deviation, if any, justified (Step 3)

If any check fails, surface the gap to the user and ask before proceeding rather than blocking. The user retains the authority to skip; the discipline is in surfacing, not in enforcement.

When the host exposes `AskUserQuestion`, render the checkpoint as a structured multi-choice card listing the six checks with their yes/no/n.a. status plus a final `proceed / pause for more research` selector — instead of dropping the verdict as plain text. Falls back to numbered text on hosts without the primitive (Codex).

## Output contract

A single message structured as:

```
## Recortte
<one sentence>

## Source A — official documentation
- <lang/lib>: <URL@version> — <one-line summary>

## Source B — validated open-source examples
- <repo>:<path>:<line-range> — <one-line summary>
  ```
  <quoted code block>
  ```

## Source C — in-repo examples
- <file>:<line> — <one-line summary>
- (or: "no analog found in the codebase")

## Source D — git history
- <commit-sha> <touching-path> — <one-line summary>
- (or: "no prior attempt found")

## Happy path
<one paragraph synthesizing A + B + C + D, with citations>

## Proposed implementation vs happy path
- aligned: <what stays canonical>
- deviates: <list of deviations>
  - <deviation>: <irrefutable justification>

## Confidence checkpoint
- A consulted: yes / no — <gap if no>
- B consulted: yes / no — <gap if no>
- C consulted: yes / no — <gap if no>
- D checked: yes / no — <gap if no>
- happy path declared: yes
- deviations justified: yes / no / n.a.
```

No code is written by this skill. The output feeds the next turn (or `/agentic-task`, `/agentic-philosophy`'s Goal-Driven Execution, or freeform implementation).

## Next

- Implement per the synthesized happy path. Cite the sources you grounded against in commit messages or task `Notes`.
- `/agentic-task` if the work needs explicit decomposition into checkbox-toggle work units.
- `/agentic-review main..HEAD` (or current scope) before merge — WORKFLOW §10.
- `/agentic-adr` if the deviation gate surfaced a binding architectural decision.
