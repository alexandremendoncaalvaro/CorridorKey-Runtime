# Pragmatic Workflow: Engineering With LLMs

CorridorKey uses LLM agents to speed up implementation without weakening the
project's architecture, release, or review controls. The workflow keeps context
small, turns important decisions into stable repo artifacts, and verifies work
with deterministic gates instead of persuasion.

## TL;DR

1. Context is the product. Small, relevant context beats large, noisy context.
2. Spec before code. Define rules, constraints, acceptance criteria, and the
   expected output before implementation.
3. Docs are for why; code is for what. History lives in git.
4. Real examples beat generic instructions. Cite the specific local file to
   follow.
5. Know the canonical path before deviating from it.
6. State the finish line, then let the agent choose the implementation path.
7. Pin load-bearing decisions in `AGENTS.md`, `ARCHITECTURE.md`, and ADRs.
8. A good prompt has a stop condition.
9. Plan before execution: explore, plan, implement, verify.
10. Review with distance. Fresh context catches the bugs the authoring context
    defends.
11. Quality gates must be deterministic.

## 1. Spec-Driven Design

Keep one topic per Markdown file. `AGENTS.md` is operational context: how to
build, test, follow conventions, and respect boundaries. `CLAUDE.md` may mirror
that file or import it with `@AGENTS.md`.

Canonical specs are constraints. `ARCHITECTURE.md` defines system patterns and
boundaries. `DESIGN.md` defines the frontend visual contract. ADRs under
`doc/adr/*.md` record binding decisions. Tasks under `doc/tasks/*.md` hold
multi-step execution plans and acceptance criteria.

Skills are on-demand context. A `SKILL.md` description is cheap session context;
the body loads only when invoked.

## 2. Docs vs. Code

Docs define intent, constraints, contracts, and decisions. Production behavior
lives in code. Comments justify non-obvious choices; they do not restate what a
line does.

## 3. Format by Evidence

Use Markdown for repo docs and skills, YAML for metadata, XML-style tags when a
prompt mixes instructions with retrieved context, and JSON only when a schema or
machine consumer needs it. Structure reduces ambiguity, but it does not replace
clear thinking.

## 4. Find the Happy Path

Before implementing unfamiliar work, identify the canonical way to do it for the
stack and cite the source. If the implementation deviates, name the reason.

## 5. Ground in Real Patterns

Do not dump the repo into context. Find a project-relevant example and follow
its structure. Cite files and paths, then read more only when needed.

## 6. Explore, Plan, Implement, Commit

For non-trivial changes:

1. Explore read-only and build the model.
2. Write or update a task file when the work spans multiple steps.
3. Implement the approved path.
4. Verify each acceptance criterion.
5. Commit one logical change when asked to commit.

## 7. Action Commands With Stop Criteria

Commands should say what to do, what not to do, and where to stop. A stop
condition prevents the agent from expanding into adjacent work.

## 8. Architectural Boundaries

Lock load-bearing decisions into `AGENTS.md`, `ARCHITECTURE.md`, and accepted
ADRs. The agent follows specified boundaries and invents what is missing, so
prefer specifying the boundary once.

## 9. Outcome-Based Prompting

State the raw input and exact expected output first. Then ask for the algorithm
or implementation that connects them. When tests are relevant, name the tests
that cover the changed surface before editing.

## 10. Reviewer With Fresh Context

The context that produced a change is biased toward defending it. Review with a
fresh context that receives only the diff and the applicable spec slice:
`AGENTS.md`, `ARCHITECTURE.md`, accepted ADRs, relevant task criteria, and recent
commit messages.

For Claude Code, use the bundled `.claude/agents/fresh-context-reviewer.md`
subagent through `/agentic-review`. For Codex, `/agentic-review` assembles a
handoff, then the user clears context and pastes the handoff into the fresh
session.

## 11. Quality Gates

Text instructions are advisory; checks are deterministic. Use hooks, formatters,
linters, tests, CI, benchmark comparisons, and release validation scripts for
rules that must not be skipped. Never bypass gates with `--no-verify`.

## 12. Discrimination Over Generation

Agents generate quickly. The engineering work is deciding what is almost right
but still wrong: bugs, spec drift, coupling, missing edge cases, and insufficient
tests.

## 13. Evals for Autonomous Work

When an agent makes decisions without continuous review, preserve observability:
tool trajectory, intermediate artifacts, failures, and final output. Treat the
prompt, scaffold, and model as the unit under test.

## 14. Staged Spikes With Golden Fixtures

When the technique is uncertain, split the work into staged spikes. Use curated
fixtures with expected outputs, emit debug artifacts per stage, and verify both
stage-level behavior and the end-to-end result.
