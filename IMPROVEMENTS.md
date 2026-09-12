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

### IMP-002: Define `signature_literal` so non-totalistic rules can be written in the DSL
**Status:** suggested
**Found:** 2026-09-12 (planning the rule library)
**Location:** SPEC.md §7; `src/rule/dsl.cpp`
**Effort:** medium
**Description.** SPEC §7's grammar names `signature_literal` as a condition form and never defines it, so a non-totalistic rule can only be built as a hand-made IR. Langton's self-reproducing loops (xscreensaver `loop`) is the concrete case: 8 states, von Neumann, 219 rotation-symmetric transitions written as `CTRBL -> N` in the literature, plus an implicit "no match retains" default.
**Proposal.** A literal is the ordered neighbour states in canonical order, e.g. `0: [1, 0, 2, 0] -> 3;` for N=4, with `_` as a wildcard per position and an optional `rot` flag that expands a statement to its rotations (which is how the loop tables are published). Statements expand into the table exactly as count conditions do; first match wins. The canonical order is SPEC §3's, so the literal's meaning is pinned by the spec already.
**Trade-offs.** Rotational expansion is only well defined for the four von Neumann neighbours and the eight Moore ones at radius 1; the syntax should refuse it elsewhere rather than guess. A large literal table is slow to expand naively (8⁴ per statement per own state is fine; Moore r=2 is not) — bound it with a diagnostic.
**Notes.** Until this lands, the loop rule can enter through the Lua front end (F-008) computing the table, which may be the better home for a 219-line rule anyway. Either way the rule library (F-010) needs one of them.

Note that candidate *features* live in [FEATURES.md](FEATURES.md) §Candidate features, and choices between design alternatives live in [DECISIONS.md](DECISIONS.md). This file is for internal changes that are neither: "is this worth doing at all?" rather than "which alternative?" or "is this user-visible?"

## Applied

*None.*

## Declined

*None.*

## Deferred

*None.*
