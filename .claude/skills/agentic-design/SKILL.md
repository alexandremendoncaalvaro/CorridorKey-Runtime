---
name: agentic-design
description: Generate DESIGN.md at the repo root by scanning existing design tokens (Figma export, tailwind.config, tokens.json, stylesheet) and synthesizing W3C-compatible YAML frontmatter + Markdown rationale per the Google Labs design.md spec. Use when the user wants to bootstrap, scaffold, generate, document, or audit DESIGN.md (visual contract — colors, typography, spacing, motion). Frontend projects only.
allowed-tools: Read, Write, Glob, Grep, Bash
---

# /agentic-design

Produces `DESIGN.md` at the repo root. Spec: [github.com/google-labs-code/design.md](https://github.com/google-labs-code/design.md). Format: YAML frontmatter (W3C-compatible `$value`/`$type` tokens) + Markdown body (rationale, do's/don'ts).

There is no template — DESIGN.md is bootstrapped from existing tokens. **No fabrication.** If a category has no source token, leave the section as `<TODO: not yet wired>`.

## Step 0 — Detect mode

Inspect the repo:

* `DESIGN.md` exists at the repo root → **audit** mode. **Do not rewrite it.** Stop after producing a drift list (see Step 4).
* `DESIGN.md` absent → **bootstrap** mode. Find the source tokens, extract, synthesize, ask only the gaps.

## Step 1 — Find the token source

Look in this order, taking the first that exists:

* `tokens.json` at the repo root or under `design/`, `src/design/`, `src/styles/`.
* `tailwind.config.{js,ts,cjs,mjs}` — extract from `theme` and `theme.extend`.
* CSS custom properties (`--color-*`, `--space-*`, etc.) in `src/**/*.css`, `src/**/*.scss`, `tokens.css`, `globals.css`.
* `figma.config.*`, Code Connect mappings, or a Figma URL the user provides.
* Component library setup (Material UI theme, Chakra UI theme, Radix tokens, Mantine theme).

If no source is found, **stop and ask the user** for one. Do not invent tokens.

## Step 2 — Extract tokens into YAML frontmatter

Build YAML using W3C Design Tokens shape (`$value`, `$type`, optional `$description`). Group into the canonical sections:

* Colors (semantic + raw palette)
* Typography (families, sizes, weights, line heights)
* Layout (spacing scale, breakpoints, container widths)
* Elevation & Depth (shadows, z-index)
* Shapes (border radii)
* **Motion** (easings, durations) — not in the official spec yet but include if any are defined.

## Step 3 — Synthesize the Markdown body

Write rationale per token group: when to use which token, what the constraint is, do's and don'ts. Cite the source file at the top so future readers know where the tokens came from. Skip prose for any group whose tokens were not present in the source.

If the project uses Figma without Code Connect, flag that the agent guessed at any component → token mapping. Suggest setting up Code Connect.

## Step 4 — Validate

Run `npx @google/design.md lint DESIGN.md` if the user has it (offer to install). Fix errors before writing.

**Audit-mode override:** do **not** write the file. Produce a drift list comparing each documented token against the current source. Format:

```
[token group / token name]: spec says X, source says Y. Suggested resolution: change spec / change code / discuss.
```

If the source has tokens DESIGN.md doesn't document, list those too as additions. If DESIGN.md documents tokens the source no longer has, list as removals.

## Output contract

A single `DESIGN.md` at the repo root. YAML frontmatter uses W3C `$value`/`$type` shape. Markdown body has one section per token group present in the source. No invented tokens. No "External Resources" section. In audit mode: a drift list, no file written.
