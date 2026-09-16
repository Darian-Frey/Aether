# Improvements

Catalogue of code-quality improvements, refactors, and architectural changes proposed during development. Per Maintenance Rule 8, improvements are logged here when noticed, not silently applied. The author decides whether to apply, defer, or decline.

This is the dual of [BUGS.md](BUGS.md): bugs are broken; improvements work but could be better.

Status vocabulary: suggested | applied | declined | deferred.
Effort vocabulary: trivial | small | medium | large.

Entries are kept in ID order within each section. Entry format:

```markdown
### IMP-NNN: {short title}
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

### IMP-004: the Seed button sits below the controls it is the point of
**Status:** suggested
**Found:** 2026-09-15 (the author asked for a seed button that already existed)
**Location:** `src/ui/panels.cpp` (`Grid` section)
**Effort:** trivial
**Description.** The Grid section runs: size fields, a separator, "Random fill density", up to sixteen sliders, a total warning, "even spread", and only then the `Seed` button with Clear, Fit and Fill view beside it. The sliders are the rarely touched part and the button is the thing a user reaches for every few minutes, so the section is ordered opposite to how it is used. It is discoverable enough that it was requested as a new feature by someone who has been using the application for a week, which is the clearest evidence available that its placement is wrong.
**Proposal.** Put the action row — Seed, Clear, Fit, Fill view — directly under the size fields, above the density block, and collapse the sliders behind a "Random fill density" tree node closed by default. The `R` shortcut is unchanged.
**Trade-offs.** The densities become one click further away, and the relationship between the sliders and what Seed does gets less obvious when they are not adjacent — the button would want a tooltip naming the densities it is about to use. Moving a control the author has learned the position of is a cost paid once.
**Notes.** Raised alongside F-028, which adds seeding of a dragged region; if both land, the action row carries two seed gestures and is worth laying out once rather than twice. Doing this before F-028 is fine and doing it after avoids moving the same widgets twice.

## Applied

### IMP-001: Outer-totalistic tables are oversized for rules that count a single state
**Status:** applied
**Found:** 2026-09-11 (Phase 1, sizing generations rules for the DSL parser)
**Applied:** 2026-09-14
**Location:** SPEC.md §4 (`Kind`), §5; `src/rule/table_layout.cpp`
**Effort:** medium
**Description.** `outer_totalistic` encodes the full count vector over states `1 … S−1`, so the table has `S·C(N+S−1, S−1)` entries. Generations rules (`B/S/C`) and cyclic CAs depend on the count of exactly one state, yet pay for the full vector: Brian's Brain (C=3, N=8) is 135 entries, fine; a C=25 generations rule is 2.6×10⁸ and a 14-state cyclic CA is 2.8×10⁶, both pushed onto the codegen backend by a representation cost rather than a rule cost. Under Phase 1 (table backend only) those rules cannot run at all.
**Proposal.** A kind or a flag — say `outer_totalistic` with an optional *counted set* of states — whose signature is `(own_state, count of neighbours in the set)`, giving `S·(N+1)` entries for every generations, cyclic, Life-like and ageing-tail rule. Both backends would gain a third index scheme, simpler than either existing one. Widened from "one counted state" to "a counted set" on 2026-09-14: an ageing tail (F-025) counts *live* neighbours, which is a set of states rather than one, and the same generalisation covers everything the narrower form did.
**Trade-offs.** Changes the IR schema (SPEC §4), which is out of scope without a DECISIONS entry, and adds a kind that both execution paths must implement identically (AV-007). Doing nothing means the Phase 1 acceptance set (Brian's Brain, a cyclic CA of ≤ 8 states) still works, and larger rules wait for Phase 4 codegen.
**Notes.** The DSL parser emits an `Expression` instead of a `Table` when the table would exceed the threshold (SPEC §7), so the rule is still representable; it just cannot execute until the expression backend exists. Raised in priority by F-025 on 2026-09-14: this is what caps an ageing tail at 6 states on 2D Moore r=1. With a counted set the same rule is `S·(N+1)` = 90 entries against 243,100, and tails of any length up to 256 states become free.

**As built (2026-09-14, D-016).** A new kind `counted_totalistic` rather than a flag, so a rule's kind still determines its indexing scheme. The set is chosen per own state, which the proposal did not anticipate and which is what lets cyclic rules — where each state counts its successor — use the form. The DSL detects it from the statements, Lua declares it, `decay` propagates it. Used only where it is strictly smaller, so binary rules keep the IR and hash they had.

### IMP-002: Define `signature_literal` so non-totalistic rules can be written in the DSL
**Status:** applied
**Found:** 2026-09-12 (planning the rule library)
**Applied:** 2026-09-14
**Location:** SPEC.md §7; `src/rule/dsl.cpp`
**Effort:** medium
**Description.** SPEC §7's grammar names `signature_literal` as a condition form and never defines it, so a non-totalistic rule can only be built as a hand-made IR. Langton's self-reproducing loops (xscreensaver `loop`) is the concrete case: 8 states, von Neumann, 219 rotation-symmetric transitions written as `CTRBL -> N` in the literature, plus an implicit "no match retains" default.
**Proposal.** A literal is the ordered neighbour states in canonical order, e.g. `0: [1, 0, 2, 0] -> 3;` for N=4, with `_` as a wildcard per position and an optional `rot` flag that expands a statement to its rotations (which is how the loop tables are published). Statements expand into the table exactly as count conditions do; first match wins. The canonical order is SPEC §3's, so the literal's meaning is pinned by the spec already.
**Trade-offs.** Rotational expansion is only well defined for the four von Neumann neighbours and the eight Moore ones at radius 1; the syntax should refuse it elsewhere rather than guess. A large literal table is slow to expand naively (8⁴ per statement per own state is fine; Moore r=2 is not) — bound it with a diagnostic.
**Notes.** Until this lands, the loop rule can enter through the Lua front end (F-008) computing the table, which may be the better home for a 219-line rule anyway. Either way the rule library (F-010) needs one of them.

Note that candidate *features* live in [FEATURES.md](FEATURES.md) §Candidate features, and choices between design alternatives live in [DECISIONS.md](DECISIONS.md). This file is for internal changes that are neither: "is this worth doing at all?" rather than "which alternative?" or "is this user-visible?"

**As built (2026-09-14).** The syntax is as proposed: elements in the canonical order of SPEC §3, `_` as a wildcard, an optional `rot`. Rotation is a quarter turn on square lattices and a sixth of a turn on hexagonal ones, derived from the offset list rather than hard-coded, so it works at any radius; it is refused in 1D and 3D. Literals and count conditions may be mixed with `and` and `or`, which the proposal did not anticipate and which cost nothing. A rule whose table exceeds the threshold is refused rather than lowered to an expression, since no backend can run one yet; the diagnostic names the size. Langton's loops is not bundled: its 219 transitions are a data item for F-010, and inventing them would be worse than leaving the slot empty.

### IMP-003: `/C` and `decay` are two implementations of one idea
**Status:** applied
**Found:** 2026-09-14 (implementing F-025)
**Location:** `src/rule/dsl.cpp` (`buildLifeLikeTable`), `src/rule/decay.cpp`
**Effort:** small
**Description.** The Generations shorthand builds its refractory chain inside `buildLifeLikeTable`, and `decay N;` builds the same chain through `applyDecay`. A test asserts the two produce identical tables, so they agree today, but a change to the ageing semantics would have to be made twice or they would drift apart silently.
**Proposal.** Build the two-state base rule for `B/S`, then route `/C k` through `applyDecay(k − 2)`. The existing Generations tests then cover both paths.
**Trade-offs.** `B/S/C` with a large `C` currently lowers to an `Expression` when the table is too big, while `applyDecay` refuses; routing `/C` through the transform would need the refusal to fall back to the existing lowering, or would change the behaviour of oversized Generations rules. That interaction is why it was not done as part of F-025.
**Notes.** Worth doing when the codegen backend lands and the lowering path stops being a dead end.

**As built (2026-09-14).** Done as part of D-016, which made it worth doing: routing `/C` through `applyDecay` means a Generations rule of any length compiles to a counted table, where before `B2/S/C25` lowered to an expression no backend could run. The oversized-lowering interaction that deferred this no longer arises.

### IMP-005: `cpuStep` has no per-cell entry point
**Status:** applied
**Found:** 2026-09-15 (planning the cell inspector, F-030)
**Applied:** 2026-09-16
**Location:** `src/sim/cpu_step.cpp` (`cpuStep`)
**Effort:** small
**Description.** The oracle computes, for every cell of every generation, precisely what somebody would want to know about one cell: the neighbour states it gathered, the count vector or table index it derived, the entry or clause that fired, and the state that came out. All of it is local to the loop body and thrown away. Anything else that wants it — the inspector of F-030, a diagnostic for a failing equivalence case, a future rule debugger — has to recompute it, and recomputing it means a second implementation of the index arithmetic.
**Proposal.** Extract the loop body into a function over (rule, spec, coordinate, read buffer) returning the next state together with the working that produced it. `cpuStep` becomes a loop over that function. No new tests are needed to cover it: every existing equivalence case exercises it the moment it exists.
**Trade-offs.** The oracle is deliberately "serial, unoptimised, and obviously correct", and a per-cell struct of working is a host allocation per cell if written carelessly — invariant 8 applies to the CPU path as much as the GPU one, so it must be a plain aggregate filled in place. Returning the working unconditionally also charges the step loop for something almost every caller discards; if that shows in the oracle's runtime, the explaining half moves behind a second entry point and the saving is lost.
**Notes.** A prerequisite for F-030 rather than a free-standing improvement, but it earns its place on its own: a failing equivalence case today reports which cell disagreed and nothing whatever about why.

**As built (2026-09-16).** Results and scratch were split rather than returned together, which the proposal did not distinguish and which is what keeps the allocation trap shut: `CellTransition` is plain data returned by value (the state read, what the rule alone gives, what is written, whether mutation overrode it, the table entry that fired, and the one scalar a kind reduced the neighbourhood to), while the buffers live in a caller-owned `StepScratch` sized once from the rule and reused for every cell. `cpuStep` makes one and loops. The feared cost did not appear: 512² × 200 generations of Life on the CPU path measured 1.83 s before and 1.75 s after, back to back on the same machine, so the second entry point the trade-off hedged about was not needed.

Two things came out of it beyond the refactor. The equivalence failure message now describes the disagreeing cell — coordinates, whether it is on a real edge, its neighbours in canonical order, the entry that fired and whether mutation overrode the result — instead of naming a cell index, which was the entry's stated standalone justification. And a new test runs `stepCell` over a whole grid against what `cpuStep` writes, across all four table kinds and the expression form, with mutation on; it is the AV-017 guard in embryo, and its coverage assertion immediately caught two wrong assumptions about which kind a rule compiles to.

### IMP-006: the session cell codec expands float data rather than compressing it
**Status:** applied
**Found:** 2026-09-16 (Phase 5 step 1, taking the codec to bytes)
**Applied:** 2026-09-16
**Location:** `src/sim/session.cpp` (`encodeCells`)
**Effort:** small
**Description.** `encodeCells` emits two bytes per run, so data with no runs costs two bytes per input byte, and base64 adds a third on top: 2.67x the input at worst. For `u8` grids this never bites, because a cellular automaton grid is mostly quiescent and runs are long. An `f32` grid is the opposite case. The encoder walks the interleaved float bytes, where the sign and exponent bytes repeat but the mantissa bytes do not, so runs break every few bytes and a field that is visually smooth still encodes as near-incompressible. A 512-square float grid is 1 MB and would inline at roughly 2.7 MB of base64 if it were under the threshold.
**Proposal.** Either pick the encoding per buffer — emit `raw` inline as base64 when the run-length pass comes out longer than the input, which is one comparison and a second encoding name the reader already understands — or make the codec stride-aware so it run-length encodes each byte lane of a float separately, where the exponent lane does compress.
**Trade-offs.** The first is barely any work but leaves float grids larger in the file than they need to be. The second complicates a codec whose present virtue is that it is eight lines and obviously correct, and it would need the stride in the file so a reader knows how to undo it, which is a format change. Doing nothing is also defensible: the byte-based sidecar threshold added the same day already routes any `f32` grid above 1M cells to a raw file, so the expansion only ever applies to small grids where 2.7x of very little is still very little.
**Notes.** Found while making the codec work in bytes for Phase 5 rather than in cells. Nothing is wrong today; this is the encoder meeting data it was not designed for, and the threshold change limits the blast radius on its own.

**As built (2026-09-16).** The first proposal, and it needed one change: `raw` already names the sidecar, so the inline uncompressed form is a third encoding, `bytes`. `encodeCells` returns the encoding alongside the data and picks whichever is shorter by measuring, rather than by guessing from the cell type — which is better than the proposal, since a quiescent float grid still compresses and a noisy `u8` one still does not. The stride-aware alternative was not built: it would have been a format change for a case the byte-based sidecar threshold already handles. Reading is now stricter in one respect the entry did not anticipate — a `state` block with an unrecognised encoding used to be skipped in silence, leaving a session loaded with no current grid, and is now an error.

## Declined

*None.*

## Deferred

*None.*
