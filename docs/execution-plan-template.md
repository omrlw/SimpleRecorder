# PLANS.md template

Use this for multi-step, risky, or architecture-touching work. Keep temporary execution plans here, not in `AGENTS.md`.

```md
# Task
One-sentence statement of the change.

## Goal
What should be true when the task is complete?

## Constraints
- Keep module boundaries intact.
- Keep `docs/current-scope.md` unless explicitly expanding the roadmap.
- Prefer contract-first and stub-safe changes.

## Files likely to change
- path/to/file
- path/to/file

## Plan
1. Inspect the relevant module and the nearest `AGENTS.md`.
2. Read the supporting doc for the area being changed.
3. Make the smallest set of edits that satisfies the task.
4. Run the relevant restore/build/test checks.
5. Review for boundary violations and doc drift.

## Verification
- build command(s)
- runtime/manual check(s)
- architecture sanity checks

## Notes
Add short discoveries here while working. Remove stale notes when done.
```
