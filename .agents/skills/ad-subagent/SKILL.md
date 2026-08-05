---
name: ad-subagent
description: Draft a new Codex custom subagent at .codex/agents/<name>.toml, using the official Codex subagents format. Use when the user wants to create, write, draft, or scaffold a custom Codex subagent for delegated work (fresh-context reviewer, codebase explorer, docs researcher, test designer, bug reproducer, bounded implementation worker). Asks one question per missing field; never invents roles or tool sets.
summary: Draft a host-specific custom subagent for bounded delegated work — Claude Code `.claude/agents/<name>.md`, Codex `.codex/agents/<name>.toml`.
---

<background_information>
Drafts `.codex/agents/<name>.toml` (project) or `~/.codex/agents/<name>.toml` (personal). Spec: https://developers.openai.com/codex/subagents.

Custom Codex agents are standalone TOML files. Required fields: `name`, `description`, `developer_instructions`. Optional fields such as `model`, `model_reasoning_effort`, `sandbox_mode`, `mcp_servers`, `skills.config`, and `nickname_candidates` inherit from the parent session when omitted.
</background_information>

<instructions>
Step 1 — confirm target.
- Name: lowercase identifier, preferably snake_case or kebab-case. This is the agent identity Codex uses when spawning or referring to it. Use the same name for the filename: `.codex/agents/<name>.toml` unless the user has an existing host convention.
- Personal or project: `~/.codex/agents/<name>.toml` (personal, shared across projects) vs `.codex/agents/<name>.toml` (project, committed). Default: project.

Step 2 — delegation-fit gate. Build a custom subagent only when at least one is true:
- The role repeats often enough to justify a reusable prompt.
- The work is self-contained and can return a summary or bounded patch.
- The role needs tool or sandbox restrictions (for example read-only review).
- The role needs a different model, reasoning effort, MCP server, or skill preload.

Do not build a custom subagent for a one-off question, frequent back-and-forth, a tightly coupled implementation step, or work whose next action blocks on the answer. Use the main session or built-in `explorer` / `worker` instead.

Common narrow shapes:
- Fresh-context reviewer: read-only, adversarial, returns findings only.
- Codebase explorer: read-only path mapper; prefer built-in `explorer` unless the repo needs a persistent role.
- Docs researcher: read-only, uses a docs MCP/server when configured, returns citations.
- Test designer: reads spec/task, proposes public-interface tests, does not implement production code.
- Bug reproducer: creates or describes the smallest failing loop, then stops.
- Bounded worker: owns a disjoint file/module set and returns changed paths plus verification.

Step 3 — interview to fill. Ask one question per missing field, in this order:
- Role: one sentence, "You are a <role> that <does X> when <triggered by Y>."
- Description: the routing signal. Specific, includes the task framings the parent agent would recognize.
- Developer instructions: role, scope, allowed sources, output format, stop criterion, and what not to do.
- Sandbox: read-only, workspace-write, or inherited. A reviewer should be read-only.
- Model / reasoning effort: omit unless the user has a reason to override parent defaults.
- Optional MCP / skills: include only when the role cannot work without them.

Do NOT invent values. When the user does not know something, ask. Do not invent TOML fields not supported by the Codex subagents docs.

Step 4 — write the file.

Path: `.codex/agents/<name>.toml` or `~/.codex/agents/<name>.toml`.

Use TOML. For multi-line `developer_instructions`, triple-quoted strings preserve indentation, so dedent the body to column 0. If a convention from `AGENTS.md` is load-bearing for the subagent, restate it or point to the exact file path; do not rely on implicit parent-session memory.

Template:

```toml
name = "<name>"
description = "<when Codex should use this agent>"
# model = "gpt-5.4"                 # optional; omit to inherit parent default
# model_reasoning_effort = "high"   # optional
# sandbox_mode = "read-only"        # optional
developer_instructions = """
You are a <role>.

Scope:
- <what this agent owns>

Do:
- <allowed action>

Do not:
- <explicit non-goal>

Output:
- <format the parent session expects>

Stop when:
- <stop criterion>
"""
```

Step 5 — stop after writing. Do not dispatch to the new subagent and do not test it. The user will exercise it themselves.
</instructions>

<output_contract>
A single new TOML file at `.codex/agents/<name>.toml` (or `~/.codex/agents/<name>.toml`). Required fields present: `name`, `description`, `developer_instructions`. Optional fields declared only when the user chose them. Body is terse, imperative, and has explicit scope, output format, and stop criterion. No unsupported TOML fields. No external dependency the user did not ask for.
</output_contract>

## Next

- Test the new subagent by asking Codex explicitly to spawn it for the workflow it serves.
- Document project-wide subagents in `AGENTS.md` if their use changes normal repo workflow.
- Use `/ad-task` to mark work AFK only when the task gives a bounded context packet and disjoint write scope.
