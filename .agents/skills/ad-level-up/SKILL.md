---
name: ad-level-up
description: |
  Run this skill when the user explicitly invokes `/ad-level-up` or names it, or asks to evolve the project's rule-set — "add a convention", "update the rules", "new rule", "merge these rules", "we keep hitting X, make it a rule", "retire this rule" — or hands over a rule gap surfaced by `ad-audit`. Companion to `ad-audit`: where ad-audit audits against the rules, this evolves them, leanly. This skill is user-invoked (`allow_implicit_invocation: false`) because it can write to the rule-set — invoke it explicitly with `/ad-level-up` or by naming it, not from inferred conversation.
  Mechanical shape: ONE pass in the current session. Every candidate clears four anti-overfitting gates plus an effectiveness pass or is rejected out loud; the drafted candidate then passes an adversarial multi-lens review; and NOTHING is written without the user's explicit approval, one item at a time. It presents a proposal with a plain-language rationale and applies only on your OK. Writes land in the curated rule-set — the ADR-0035 machine store for you-everywhere conventions, or the ADR-0043 project layer (`.agentic/rules/`) for this-project conventions.
summary: Human-gated rule-set curation, companion to ad-audit. Four anti-overfitting gates + effectiveness pass + adversarial multi-lens review per candidate, then a HARD human gate — never writes unprompted. Targets the ADR-0035 machine store or ADR-0043 project layer. Ships a rule-candidate-reviewer subagent.
---

<how-this-runs-on-codex>
Codex skills run inline in the current session. This skill can write (via edits), so its safety rests entirely on the human gate below, not on tool restriction. The default pass runs the gates, the effectiveness pass, and the adversarial multi-lens review inline; the optional escalation runs the lens reviews as user-spawned subagents for true isolation.

Mechanical shape:

```
THIS SESSION:
  1. State the candidate + its citation (or stop — not grounded).
  2. Trace it to a root cause.
  3. Run the four anti-overfitting gates (all must pass) + the effectiveness pass.
  4. Place it deterministically; draft the minimal edit (do NOT write yet).
  5. Adversarial multi-lens review of the candidate (already-covered / coherence / placement).
  6. PRESENT the proposal + rationale. Apply ONLY on explicit approval, one item at a time.
```

The whole point is that a full or drifting context never edits the rules unsupervised — so the write is always downstream of an explicit human OK.
</how-this-runs-on-codex>

<prime-directive>
HARD human gate: NEVER write to the rule-set without explicit human approval. Always present the proposal plus a plain-language rationale first; apply only on the user's OK, one item at a time. No batch writes, no "while I'm here" edits. This gate is enforced by this contract — hold to it even though the session can write.
</prime-directive>

<anti-patterns>
- Do NOT write, edit, or create any rule file before the user explicitly approves that specific item.
- Do NOT add a rule that an existing rule (or a repo binding doc) already covers — merge or sharpen instead.
- Do NOT add a rule for a lone, uncited, cheaply-caught slip — that is a note, not a rule (each extra rule lowers adherence to all the others).
- Do NOT invent a rule-set location — machine store resolves `$AGENTIC_RULES_DIR` / `~/.agentic/rules/`; project layer is `.agentic/rules/` at the repo root; if the target layer is absent, offer to create it on approval.
- Do NOT put the project layer in `.gitignore` when the user picks machine-local visibility — the exclusion goes in `.git/info/exclude` (per-clone, never committed).
- Do NOT route an architectural decision or an engineering standard here — send it to `/ad-adr` or `/ad-guidelines`.
- Do NOT begin any file I/O before printing the Step 0 announce line.
</anti-patterns>

<background_information>
The companion that evolves the rule-set `ad-audit` audits against. Curation is a WRITE operation behind a human gate — distinct from the read-only `ad-audit`. The mechanism (four anti-overfitting gates, effectiveness pass, deterministic placement, adversarial multi-lens review, hard human gate) is ADR-0037; the rule-set locations are ADR-0035 (machine store) and ADR-0043 (project layer at `.agentic/rules/`, committed or machine-local via `.git/info/exclude` — a project rule shadows a conflicting machine-store rule, and the audit reports the shadowing). On Claude Code the multi-lens review fans out `Task` subagents; on Codex it runs inline with an optional user-initiated `rule-candidate-reviewer` escalation. The skill owns the terse rule-set only — bigger decisions route to `ad-adr` / `ad-guidelines`.
</background_information>

<instructions>
Step 0 — announce. Print the shape before any work:

```
Running ad-level-up (Codex single-pass, human-gated). I will state the candidate + evidence, trace its root cause, run the four anti-overfitting gates + effectiveness pass, place it, draft the minimal edit, run an adversarial multi-lens review, then PRESENT a proposal. I will NOT write anything to the rule-set until you explicitly approve — one item at a time.
```

Step 1 — state candidate + evidence. One sentence + the citation (finding / PR / transcript / file:line, or the `ad-audit` handoff). If it cannot be cited, stop — not grounded.

Step 2 — trace to root cause. Attach the candidate to the upstream cause (investigation / grounding / verification gap), not the surface symptom.

Step 3 — four anti-overfitting gates. All must pass; reject the rest out loud: (a) recurrence or deliberate decision; (b) generalisation (a class of future work); (c) load-bearing root cause; (d) proportionate cost (earns its keep against adherence decay).

Step 4 — effectiveness pass. Classify (improvement / correction / increment / merge / extend / reject — prefer sharpening/merging/extending). Check redundancy (does an existing rule / binding doc already watch this?), real-behaviour (a cited observed defect, not a hypothetical), and dead-rule sweep (flag a subsumed rule for retirement).

Step 5 — deterministic placement. Assign to the group whose grounding-target it matches; keep the group set minimal (new group only when a defect class is uncovered by every existing group; split only when a group grows too large); next stable id.

Step 6 — draft the minimal edit. Pick the layer from the rule's own content (generalizes beyond this repo → machine store at `$AGENTIC_RULES_DIR` else `~/.agentic/rules/`; this-project convention → `.agentic/rules/` at the repo root), stating the recommendation for the user to confirm; read the target file(s) first. On first project-rule creation ask committed vs machine-local; machine-local wires `.agentic/rules/` into `.git/info/exclude` (on approval, never `.gitignore`). When you find an `.agentic/rules/` that is neither committed nor excluded (e.g. a fresh clone of a machine-local repo), re-offer the committed-vs-machine-local choice. Draft the terse imperative rule (and its rationale when worth keeping). Do NOT write to disk.

Step 7 — adversarial multi-lens review. Refute the candidate against the actual current rule files along three lenses: (a) already-covered (cite the covering rule → reject/merge), (b) coherence + necessity (minimal change? clears the gates?), (c) placement. Run inline; for a substantive rule, recommend the user spawn the bundled `rule-candidate-reviewer` subagent once per lens for true isolation. Filter and reclassify; do not write.

Step 8 — report + gate. Present the exact delta (old → new per file, at the resolved path), the gate table, the effectiveness line, the adversarial-review verdicts (citing the covering rule on any reject/merge), a plain-language rationale, and a "considered but rejected as overfitting" list. Apply ONLY on the user's explicit approval, one item at a time; then stop and await the next item.
</instructions>

<output_contract>
- A proposal (delta + gate table + effectiveness line + adversarial verdicts + rationale + rejected-as-overfitting list) — presented, not applied.
- On explicit approval: the minimal edit to the rule-set at the ADR-0035 location, one item at a time. Nothing written without approval.
</output_contract>

<optional_escalation>
For a substantive candidate, spawn the bundled `rule-candidate-reviewer` subagent (`.codex/agents/rule-candidate-reviewer.toml`) once per lens (already-covered / coherence / placement) against the actual rule files, for true fresh-context isolation. Pass the handoff inline in the spawn prompt — the candidate, its evidence, and the resolved rule-set path (a curation candidate is small; no persisted trail file is needed). Union the verdicts into the proposal; the human gate is unchanged.
</optional_escalation>

<next>
- Re-run `/ad-audit` after a rule lands to confirm it catches what it was written for.
- For a decision bigger than a rule line: `/ad-adr` or `/ad-guidelines`.
- `ad-audit`'s Step 8 handoff is this skill's Step 1 candidate.
</next>
