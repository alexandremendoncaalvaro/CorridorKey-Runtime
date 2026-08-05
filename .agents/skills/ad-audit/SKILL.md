---
name: ad-audit
description: |
  Run this skill when the user explicitly invokes `/ad-audit` or names it ("run ad-audit", "audit against the rules"), or asks for the maximum quality gate before work reaches the team ("adversarial audit", "rules audit", "maximum gate", "verify before I post", "exhaustive review"). Heavier and more exhaustive than `ad-review` (light two-axis diff review); distinct from `ad-drift` (documentation-drift only). Auto-trigger note: `allow_implicit_invocation: true` is set, so audit-language can fire the skill; if a request is ambiguous, confirm scope before invoking.
  Mechanical shape: ONE pass in the current session. The skill resolves the project's rule-set (repo binding docs plus optional curated machine and project rule layers), walks every rule-group as a checklist with structural separation, emits an explicit verdict for every rule (pass / violation / judgement-call / n-a — none skipped, so coverage is a matrix), grounds every finding on the actual code/output, and gates every teammate-visible claim on a real evidence artifact. Never emits an "approve" verdict. Rule gaps route to `/ad-level-up`.
summary: Maximum-gate rules-anchored audit. Walks every rule-group as a checklist with structural separation, exhaustive per-rule verdicts (coverage matrix), evidence-gated, never approves. Codex reviews inline; ships an audit-group-reviewer subagent for user-initiated isolated + cross-model escalation. Heavier than ad-review.
---

<how-this-runs-on-codex>
Codex skills run inline in the current session. Codex supports subagents, but spawning is explicit user-directed orchestration, not something this skill does silently. So the default audit is one pass with disciplined per-group checklist output; the optional escalation (true isolation per group, and the cross-model pass on critical groups) uses the bundled `audit-group-reviewer` subagent against the persisted audit-trail file.

Mechanical shape:

```
THIS SESSION:
  1. Target + tree (what is under audit, and on which tree/SHA?).
  2. Resolve the rule-set: repo binding docs (always) + curated store at
     $AGENTIC_RULES_DIR or ~/.agentic/rules/ (optional) + project rules at
     .agentic/rules/ (optional). The rule-set defines the groups and any
     CRITICAL tag — never hardcode them.
  3. Enumerate every group. For each: review it, or record explicit N/A + reason.
  4. Write the assembled context to .agentic/reviews/<ISO>-audit-<scope>.md (audit trail).
  5. Review each dispatched group in this session, as a checklist. Output one
     section per group; give EVERY rule an explicit verdict.
  6. Coverage matrix + verdict (never "approve") + rule-gap handoff to /ad-level-up.
```

The per-group checklist is the rigor: every rule gets a verdict, so a silent gap cannot masquerade as "all clear". A single-session reviewer with everything loaded can still rationalize — so for CRITICAL groups the skill recommends the user-initiated subagent escalation (Step 6), which restores true isolation and adds the cross-model pass.
</how-this-runs-on-codex>

<anti-patterns>
- Do NOT emit an "approve" verdict. The audit is adversarial; nothing clears until every teammate-visible claim carries a reproducible artifact and every blocker is resolved or refuted with evidence.
- Do NOT hardcode rule content, group names, or a CRITICAL tag. They come from the resolved rule-set only.
- Do NOT skip a rule. Every rule in every enumerated group gets an explicit verdict, or the audit is invalid. A group whose subject is untouched is logged N/A-with-reason, never silently omitted.
- Do NOT clear a rule you could not ground on the actual code/output. Mark it an OPEN QUESTION.
- Do NOT accept a teammate-visible claim backed only by code-reading or inference. No run/observed artifact → BLOCKER.
- Do NOT skip writing the audit-trail file — it is the context packet for the user-spawned escalation.
- Do NOT begin any file I/O before printing the Step 0 announce line.
</anti-patterns>

<background_information>
The maximum quality gate. Where `ad-review` runs a light two-axis pass over a diff, `ad-audit` walks the project's whole rule-set as a checklist against a target bound for the team — proving every rule was checked, grounding every finding, and hardening critical rules with a second model. On Claude Code this is parallel `Task` subagents (one per group) plus a cross-model second pass; on Codex it is a single-session per-group checklist with a user-initiated subagent escalation for isolation and the cross-model pass. The rule-set location convention is ADR-0035; the mechanism is ADR-0036. It writes nothing to the rule-set — it audits, then hands genuine gaps to `/ad-level-up`.
</background_information>

<instructions>
Step 0 — announce. Print the shape before any work:

```
Running ad-audit (Codex single-pass, per-group checklist). I will resolve the rule-set (repo binding docs + optional ~/.agentic/rules/ + optional .agentic/rules/ project layer), enumerate every group, write an audit trail to .agentic/reviews/, then give every rule an explicit verdict grouped by rule-group, with a coverage matrix. I never emit "approve".

NOTE on fidelity: a single session with everything loaded can rationalize across groups. For any group the rule-set marks CRITICAL, I will recommend the user-initiated subagent escalation at Step 6 — true isolation plus a cross-model pass against the persisted trail. The escalation TOML schema is at the bottom of this skill.
```

Step 1 — target + tree. State what is under audit (a diff / branch / PR, or drafted claims/artifacts about to be posted) and which tree/SHA it rests on (`git fetch origin main`; name the SHA). If ambiguous which target, ask.

Step 2 — resolve the rule-set (three layers, ADR-0035 + ADR-0043):
- Repo binding docs (always): AGENTS.md, ARCHITECTURE.md, GUIDELINES.md, CONTEXT.md / CONTEXT-MAP.md, accepted ADRs under `doc/adr/` the target touches. Read what exists; never fabricate.
- Curated store (optional): `$AGENTIC_RULES_DIR` if set, else `~/.agentic/rules/` if it exists; read its rule files. The rule-set defines the groups and any CRITICAL tag. If only repo docs exist, treat each binding doc / accepted ADR as a group. If no rule-set resolves, stop — nothing to audit against.
- Project rules (optional): `.agentic/rules/` at the repo root, if present — same format as the machine store; committed or machine-local (`.git/info/exclude`), resolution does not care which.
- Precedence: union across layers, except on genuine conflict, where a project rule wins over a machine-store rule — apply the project rule and report the shadowed store rule as a line in the audit output (never silent).

Step 3 — enumerate all groups; dispatch or N/A. Enumerate EVERY group. Review each group the target touches; record explicit `N/A` + one-line reason for each it does not (including CRITICAL groups genuinely untouched). Cherry-picking invalidates the audit.

Step 4 — write the audit-trail handoff. Persist the assembled context (target, tree/SHA, the resolved rule-set with its groups, the CRITICAL tags) to `.agentic/reviews/<ISO-timestamp>-audit-<scope-slug>.md`. Create the dir if missing; advise `.gitignore` for `.agentic/reviews/`. This file is what a user-spawned reviewer receives.

Step 5 — review, group by group. For each dispatched group, in its own `## Group: <id>` section, walk its rules as a checklist in order. Give EVERY rule a verdict:
- **pass** (grounded) · **violation** (cite the exact rule + `file:line`/claim) · **judgement-call** (cite the named baseline) · **n-a** (subject untouched, one-line reason).
Ground on the actual code/output — read files, run/inspect output; never assume. Each finding states the concrete failure scenario AND the specific evidence artifact missing or contradicting the claim. Hard gate: a teammate-visible claim with no run/observed artifact is a BLOCKER. Re-read the group's rules before closing the section and confirm every rule has a verdict.

Step 6 — harden CRITICAL groups (recommend escalation). For every group the rule-set marks CRITICAL, recommend the user run the bundled `audit-group-reviewer` subagent against the audit-trail file. The cross-model pass is realized primarily via the kit's **dual-host split** — re-check the critical group under the other provider (Codex flavor ⇄ Claude flavor); where a single host can vary the model per call, that override is an in-host proxy. Run the cross-model pass **twice**, reordering the rules and the target's hunks between the two runs (swap-and-agree: accept a `pass` only if it holds both ways). Honest ceiling: an in-host override is intra-family and only partially decorrelates blind spots; the cross-provider path is stronger, and true cross-vendor diversity is adopted when routing allows. Union the escalation's findings in.

Step 7 — aggregate + verdict. Union all findings (never drop a lone one); confirm real ones against the code/output and reject wrong ones with evidence. Coverage check: every group accounted for by verdicts or N/A-with-reason, or the audit is INCOMPLETE. Then the verdict — NEVER "approve": list each blocker with the evidence artifact it needs; state everything unverified as an OPEN QUESTION.

Step 8 — close the loop. Hand any rule gap (a defect pattern no rule covers, or a rule that misled) to `/ad-level-up` as a candidate.
</instructions>

<output_contract>
- Audit-trail file at `.agentic/reviews/<ISO>-audit-<scope>.md`.
- One `## Group: <id>` section per dispatched group; one line per rule: `<verdict> · <rule id> · <file:line | claim> · <failure scenario> · <artifact needed or observed>`.
- A coverage matrix: every group accounted for (verdicts or N/A-with-reason).
- Blockers on top; then open questions; then any proposed rule delta for `/ad-level-up`.
- No "approve" verdict, no defending the work, no rewrite. Empty result reported explicitly.
</output_contract>

<optional_escalation>
For the §10 ideal (true fresh context) on a CRITICAL group, spawn the bundled `audit-group-reviewer` subagent (`.codex/agents/audit-group-reviewer.toml`) against the persisted audit-trail file. Configure two runs with a different `model` between them and reorder the group's rules and the target's hunks; accept a `pass` only if both agree. Union the results into the audit.
</optional_escalation>

<next>
- Resolve every blocker with the evidence artifact it named, then re-run `/ad-audit` on the fix.
- Route any rule gap to `/ad-level-up`.
- For a lighter routine diff review, use `/ad-review`. For documentation-vs-code drift only, use `/ad-drift`.
</next>
