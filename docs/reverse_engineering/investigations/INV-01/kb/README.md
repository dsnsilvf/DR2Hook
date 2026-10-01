# DR2Hook Knowledge Base
Maintained by: DR2Hook Knowledge Keeper. Target: DiRT Rally 2.0 (DR2Hook project).

Purpose: let any new agent learn what DR2Hook already knows without repeating old investigations.

## Files
- `confirmed-facts.md`  — CONFIRMED FACTS (validated; IDs `F-###`)
- `hypotheses.md`       — ACTIVE HYPOTHESES (unvalidated; IDs `H-###`)
- `open-questions.md`   — OPEN QUESTIONS (IDs `Q-###`)
- `history.md`          — append-only log: promotions, refutations, replacements, contradictions
- `glossary.md`         — canonical terminology and accepted aliases

## Rules
1. A hypothesis becomes a fact only via an explicit, logged promotion citing the validation.
2. Refuted/replaced entries are never deleted; they move to status REFUTED/REPLACED and are logged in `history.md`.
3. New evidence contradicting a confirmed fact is logged as a CONTRADICTION and flagged to the Chief (RE Orchestrator); the fact stays unchanged until the Chief rules.
4. Differing terminology from other agents is mapped via `glossary.md`, not by rewriting entries.
5. No speculation by the Keeper unless explicitly asked.

## Entry template
```
### <ID> — <short name>
- Status: CONFIRMED | ACTIVE | REFUTED | REPLACED (by <ID>) | OPEN
- Address: (module + RVA preferred, e.g. dirtrally2.exe+0x...; note game build)
- Structure:
- Function:
- Meaning:
- Evidence: (source agent, artifact/capture, date)
- Confidence: High | Medium | Low (+ reason)
- Related fields:
- Related functions:
- How validated:
- What remains unknown:
- Recorded: <date> by <agent>   Last changed: <date>
```
