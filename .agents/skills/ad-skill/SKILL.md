---
name: ad-skill
description: Draft a new Claude Code or Codex skill at .claude/skills/<name>/SKILL.md (or .agents/skills/<name>/SKILL.md for Codex), using the Anthropic Skills format. Use when the user wants to create, write, draft, or scaffold a custom skill for an agentic coding tool. Asks one question per missing field; never invents skill names or triggers.
summary: Draft a new Claude Code or Codex skill at the appropriate path.
---

<background_information>
Drafts `.claude/skills/<name>/SKILL.md` (Claude Code) or `.agents/skills/<name>/SKILL.md` plus `agents/openai.yaml` (Codex). Spec: code.claude.com/docs/en/skills. Open standard: agentskills.io. Examples: github.com/anthropics/skills.
</background_information>

<instructions>
Step 1 — confirm target. Ask the user:
- Name — kebab-case, lowercase, ≤64 chars. Becomes the directory name and the slash command.
- Target agent(s) — Claude Code, Codex, or both. Determines path and frontmatter shape.
- Personal or project — `~/.claude/skills/<name>/` (personal) vs `.claude/skills/<name>/` (project, committed). Default: project.

Step 2 — interview to fill. Ask one question per missing field, in this order:
- What it does — one sentence, primary triggering signal.
- When to invoke — common task framings the user would say. Combined `description` + `when_to_use` is capped at 1,536 chars per the Anthropic spec.
- Tools needed — `Read, Write, Glob, Grep, Bash, Task, ...` Restrict to what the skill actually uses.
- Body shape — instructions, optional template, output contract. Keep ≤500 lines; move long material to sibling files (`reference.md`, `examples.md`, `scripts/`).

Do NOT invent values. When the user does not know something, ask. Do NOT invent fields not in the spec — only declare frontmatter fields that actually apply.

Step 3 — write the file(s).

Path:
- Claude Code project: `.claude/skills/<name>/SKILL.md`
- Claude Code personal: `~/.claude/skills/<name>/SKILL.md`
- Codex project: `.agents/skills/<name>/SKILL.md` plus `.agents/skills/<name>/agents/openai.yaml` (cc-sdd convention).

Frontmatter per agent:
- Claude Code: `name`, `description`, `allowed-tools` (and any other field from the spec the user asked for).
- Codex: minimal frontmatter — `name`, `description`. Body uses XML tags (`<background_information>`, `<instructions>`, `<template>`, `<output_contract>`). The `agents/openai.yaml` carries `interface.display_name`, `interface.short_description`, `policy.allow_implicit_invocation`.

Body: imperative instructions ("do X", not "this skill does X"). Every line is recurring token cost once the skill loads — be terse. Don't restate AGENTS.md.

Step 4 — stop after writing. Do not invoke the new skill, do not pre-fill it with example content, do not test it. The user will exercise it themselves.
</instructions>

<output_contract>
A single new `SKILL.md` at the chosen path (plus `agents/openai.yaml` for Codex). Frontmatter declares only the fields actually used. Body is imperative and terse. No external file dependencies the user did not ask for.
</output_contract>

## Next

- Test the new skill's description triggers by invoking it from a real conversation — verify auto-triggering on the keywords you chose.
- If the skill is meant to be universal across the kit's profiles, propose an ADR + Task to add it to the profile catalog (`src/lib/profiles.js`).
- If the skill carries a sibling subagent, declare it in `manifest.json` and add the source under `agents/`.
