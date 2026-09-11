# Improvements

Catalogue of code-quality improvements, refactors, and architectural changes proposed during development. Per Maintenance Rule 8, improvements are logged here when noticed, not silently applied. The author decides whether to apply, defer, or decline.

This is the dual of [BUGS.md](BUGS.md): bugs are broken; improvements work but could be better.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

Entry format:

```markdown
### IMP-001: {short title}
**Status:** suggested
**Found:** YYYY-MM-DD ({session/commit context})
**Location:** {path/to/file.ext:line, or "cross-cutting"}
**Effort:** {trivial | small | medium | large}
**Description.** {What could be improved and why.}
**Proposal.** {How to do it.}
**Trade-offs.** {What we'd give up or risk. Required — without this the entry is a feature request, not a candidate improvement.}
**Notes.** {Related context, dependencies on other work.}
```

## Suggested

### IMP-001: Outer-totalistic tables are oversized for rules that count a single state
**Status:** suggested
**Found:** 2026-09-11 (Phase 1, sizing generations rules for the DSL parser)
**Location:** SPEC.md §4 (`Kind`), §5; `src/rule/table_layout.cpp`
**Effort:** medium
**Description.** `outer_totalistic` encodes the full count vector over states `1 … S−1`, so the table has `S·C(N+S−1, S−1)` entries. Generations rules (`B/S/C`) and cyclic CAs depend on the count of exactly one state, yet pay for the full vector: Brian's Brain (C=3, N=8) is 135 entries, fine; a C=25 generations rule is 2.6×10⁸ and a 14-state cyclic CA is 2.8×10⁶, both pushed onto the codegen backend by a representation cost rather than a rule cost. Under Phase 1 (table backend only) those rules cannot run at all.
**Proposal.** A kind or a flag — say `outer_totalistic` with an optional `counted_state` — whose signature is `(own_state, count of one state)`, giving `S·(N+1)` entries for every generations and cyclic rule. Both backends would gain a third index scheme, simpler than either existing one.
**Trade-offs.** Changes the IR schema (SPEC §4), which is out of scope without a DECISIONS entry, and adds a kind that both execution paths must implement identically (AV-007). Doing nothing means the Phase 1 acceptance set (Brian's Brain, a cyclic CA of ≤ 8 states) still works, and larger rules wait for Phase 4 codegen.
**Notes.** The DSL parser emits an `Expression` instead of a `Table` when the table would exceed the threshold (SPEC §7), so the rule is still representable; it just cannot execute until the expression backend exists.

Note that candidate *features* live in [FEATURES.md](FEATURES.md) §Candidate features, and choices between design alternatives live in [DECISIONS.md](DECISIONS.md). This file is for internal changes that are neither: "is this worth doing at all?" rather than "which alternative?" or "is this user-visible?"

## Applied

*None.*

## Declined

*None.*

## Deferred

*None.*
