---
name: ad-ground
description: Four-source pre-implementation research — official docs, validated implementation references (open-source repos, Stack Overflow / forum answers, blog posts, gists), in-repo patterns, and git history — then synthesize a happy path and gate any deviation with an irrefutable justification before code is written. Auto-invokes on non-trivial work, refactors, library or pattern selection, "research before coding", "before implementing", "which library", "which pattern", "how to approach", "ground before coding". Workflow-operational counterpart to WORKFLOW.md §4 (Find the Happy Path) and §5 (Ground in Real Patterns).
summary: Four-source pre-implementation research (docs / impl-refs / in-repo / git history) + happy-path synthesis + deviation gate. WORKFLOW §4 + §5.
---

<background_information>
Implements WORKFLOW §4 + §5 end-to-end as one research pass. The four sources are joined by AND, not OR — every non-trivial change runs the full research pass, then synthesizes a happy path, then justifies any deviation. Output is the input to whatever skill or freeform turn produces the implementation plan; this skill does not write code.

Codex auto-trigger on description keywords is less mature than Claude Code's. If auto-invocation does not fire on a non-trivial change, invoke this skill manually before implementing.
</background_information>

<instructions>
Step 0 — scope the research scope. Confirm what is being researched in one sentence. The research scope is the smallest verifiable surface that captures the change: a function to add, a library to pick, a pattern to apply. If broader than one sentence, ask the user to narrow. Skip the skill on genuinely trivial diffs (rename, typo, one-line refactor on a tested path).

Step 1 — four-source research pass, all four required:

Source A — official documentation. For each language and library in scope, cite the canonical doc URL and version. Read the relevant section. Ask the user for a known-good link rather than fabricating one. Output: bulleted citations, one per language/library, each with URL plus a one-line summary.

Source B — validated implementation references. ≥1 (prefer 2–3) public reference (open-source repo, Stack Overflow / forum answer, blog post, gist) solving the same technical research scope with similar techniques. Match is technical, not domain. Cite `<source>:<locator>` — `<repo>:<path>:<line-range>` for repos, `<URL>` for Stack Overflow / forum / blog / gist — and quote the relevant block. Never paraphrase from training memory. If search is inconclusive, ask the user for a known reference.

Source C — in-repo examples. Grep / glob for analogous patterns. Cite `<file>:<line>` plus a one-line description of how the existing example handles the same shape. If the codebase has no analog, state that explicitly.

Source D — git history. Run `git log --all --oneline -- <relevant-paths>`, `git log --all --grep=<keyword>`, sweep sibling active branches. Cite `<commit-sha>` plus touching file path and a one-line description. If empty, state "no prior attempt found." Narrow with `--grep` or `-S` on multi-thousand-commit repos.

Step 2 — happy path synthesis. In one paragraph, name the most-grounded approach for the research scope and cite at least one source per Source A / B / C. Source D included when it produced a hit; otherwise mark "no prior attempt found." The paragraph is the canonical answer to "what is the canonical, idiomatic way to solve this here?"

Step 3 — deviation gate. If the implementation about to be written deviates from the happy path, write the justification first. Must name the specific constraint, evidence, or trade-off forcing the deviation — generic "we want it differently" is insufficient. If the justification cannot be written confidently, loop back to Step 1 and look harder; do not deviate without it. Prescriptive gate, not descriptive — write the answer down.

Step 4 — confidence checkpoint. Soft verdict on:
- A consulted (≥1 official-doc citation per language/library)
- B consulted (≥1 implementation-reference citation, with cite-and-fetched code)
- C consulted (in-repo analog cited or "no analog found" stated)
- D checked (commits / branches surveyed; hit cited or "no prior attempt found")
- Happy path declared (Step 2)
- Deviation, if any, justified (Step 3)

If any check fails, surface the gap to the user and ask before proceeding. Do not block. The user retains authority to skip; the discipline is in surfacing.
</instructions>

<output_contract>
A single message structured as:

```
## Recortte
<one sentence>

## Source A — official documentation
- <lang/lib>: <URL@version> — <one-line summary>

## Source B — validated implementation references
- <repo>:<path>:<line-range> — <one-line summary>   # repo form
  ```
  <quoted code block>
  ```
- <URL> — <one-line summary>                         # Stack Overflow / forum / blog / gist
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

No code is written by this skill. The output feeds the next turn or another skill.
</output_contract>

## Next

- Implement per the synthesized happy path. Cite the sources you grounded against in commit messages or task Notes.
- `/ad-task` if the work needs explicit decomposition into checkbox-toggle work units.
- `/ad-review main..HEAD` (or current scope) before merge — WORKFLOW §10.
- `/ad-adr` if the deviation gate surfaced a binding architectural decision.
