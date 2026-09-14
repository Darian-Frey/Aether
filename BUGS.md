# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8, bugs are logged here when found, not silently fixed. The author decides whether to fix immediately, defer, or leave alone.

This is the dual of [ATTACK_VECTORS.md](ATTACK_VECTORS.md): attack vectors are anticipated failure modes with detection methods; bugs are realised failures with status flags. A recurring bug pattern may warrant a new AV entry; an AV entry that escaped detection becomes a bug.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

Entry format:

```markdown
### BUG-001: {short title}
**Status:** open
**Found:** YYYY-MM-DD ({session/commit context})
**Location:** {path/to/file.ext:line, or "cross-cutting"}
**Severity:** {low | medium | high}
**Description.** {What's wrong and why it matters.}
**Reproduction.** {Minimum steps to trigger.}
**Notes.** {Related context, suggested fix, links to BUG/IMP/D/AV entries.}
```

## Open

### BUG-008: default random-fill densities sum to more than one above nine states
**Status:** open
**Found:** 2026-09-14 (D-016, rendering the fourteen-state cyclic rule)
**Location:** `src/ui/app.cpp` (`applyPaletteForStates`)
**Severity:** low
**Description.** The default fill gives state 1 a density of 0.2 and every other state 0.1, so for `S > 9` the weights sum past 1. `fillRandom` walks the cumulative distribution and stops at the first threshold above its draw, so the last states are never seeded: a fourteen-state rule starts with nothing in states 10 to 13. The engine is doing what it was asked; the defaults are wrong.
**Reproduction.** `aether --rule @cyclic-14` and look at the initial grid, or press R.
**Notes.** A one-line fix — give each non-quiescent state `0.8 / (S - 1)` — but it is the author's call whether the default should be uniform or weighted toward the quiescent state. Does not affect reproducibility: the densities are journaled and replay exactly.

## Fixed

### BUG-001: SPEC §3 closed form for 3D von Neumann neighbour count is wrong
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `rule/neighbourhood` tests)
**Location:** SPEC.md §3
**Severity:** low
**Description.** SPEC gave `N = 2r(2r²+3r+2)/3` for 3D von Neumann, which evaluates to 4.67 at r=1 while the same paragraph states the r=1 answer is 6. Enumeration gives 6, 24, 62 for r=1,2,3. The correct closed form is `(2r+1)(2r²+2r+3)/3 − 1`. Documentation only; no code was written against the wrong formula.
**Reproduction.** Evaluate the old formula at r=1.
**Notes.** Fixed in SPEC the same day. The enumeration in `neighbourOffsets()` is the source of truth and the test checks the closed form against it, which is how this was caught.

### BUG-002: SPEC §5 multi-state outer-totalistic index encoding did not match its own size formula
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `rule/table_layout`)
**Location:** SPEC.md §5
**Severity:** medium
**Description.** For `states > 2` SPEC described the count vector as "a mixed-radix integer over counts of states 1…S-1" but gave the table size as the number of compositions (worked example: 4 states, N=8 → ≈660). A mixed-radix encoding with radix N+1 per digit needs `S·(N+1)^(S−1)` entries (2916 for that example), so the two statements were inconsistent, and the difference decides backend selection for every multi-state rule. Resolved in favour of the size formula, which is the more specific statement: count vectors are ranked densely in lexicographic order, giving exactly `S·C(N+S−1, S−1)` entries. `TableLayout::indexOuterTotalistic` implements the ranking and the GLSL side must implement the same one.
**Reproduction.** Compare the two sentences in the old §5 "Outer-totalistic" paragraph for S=4, N=8.
**Notes.** SPEC §5 reworded to state the ranking explicitly. This is a clarification of an ambiguous paragraph, not a change to `LUT_MAX_ENTRIES` or to the selection rule.

### BUG-003: SPEC §2 `mirror` boundary did not say which reflection
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `sim/boundary`)
**Location:** SPEC.md §2
**Severity:** medium
**Description.** "Reflect about the boundary" admits two readings: reflection about the grid edge (`−1 → 0`, which is what `GL_MIRRORED_REPEAT` does) and reflection about the edge cell's centre (`−1 → 1`). They produce different automata — under the first, a corner cell counts itself up to three times among its Moore neighbours. Both execution paths must pick the same one (AV-005), so the spec has to say which.
**Reproduction.** Place an L of three live cells in a corner under B3/S23 with `mirror`; the two readings give a block and a three-cell result respectively.
**Notes.** Resolved as reflection about the edge cell's centre, since a cell being its own neighbour has no counterpart in any automaton this project targets. `sim::resolve()` is the reference; the GLSL side must not use sampler address modes for this. The corner-L case is now a test.

### BUG-004: SPEC §5 table storage as a 1D texture cannot hold a threshold-sized table
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `sim/gpu_step`)
**Location:** SPEC.md §5
**Severity:** medium
**Description.** SPEC specified the lookup table as a 1D `GL_R8UI` texture. The NVIDIA driver reports `GL_MAX_TEXTURE_SIZE = 32768`, which bounds 1D textures too, while `LUT_MAX_ENTRIES` is 65536. A rule anywhere in the upper half of the table range would have failed to allocate on the target machine. Changed to a shader storage buffer, which has no such bound and is what the compute path already uses for its other inputs.
**Reproduction.** `glTexStorage1D(GL_TEXTURE_1D, 1, GL_R8UI, 65536)` on the T1200.
**Notes.** SPEC §5 reworded. No semantic change: the table contents and the index arithmetic are identical; only the container differs.

### BUG-005: SPEC §9.2 derived the mutated state from the hash that had just passed the threshold test
**Status:** fixed
**Found:** 2026-09-11 (Phase 2, implementing stream B)
**Location:** SPEC.md §9.2
**Severity:** high
**Description.** The pseudocode tested `h < p·2³²` and then took the replacement state from "the upper bits of `h`" by multiply-shift. A hash that passed the test is numerically small, so its upper bits are near zero and `(h·S) >> 32` is 0 for any realistic `p`. Cell mutation as specified would have set every mutated cell to state 0 — a decay process, not the uniform replacement the feature describes. Would have been invisible in equivalence tests, since both paths would have agreed on the wrong thing.
**Reproduction.** With `p = 0.001`, `h < 4.3×10⁶`, so `(h·S) >> 32 = 0` for all `S ≤ 256`.
**Notes.** The state now comes from `uniform_state(mix32(h ^ 0xA5A5A5A5))`. `tests/sim/hash_test.cpp` checks that replacement states among cells that passed a 0.1% threshold are spread across the range.

### BUG-006: SPEC §9.3 called the lineage append-only and also offered a grid rewind
**Status:** fixed
**Found:** 2026-09-12 (Phase 2, implementing sessions)
**Location:** SPEC.md §9.3, §11
**Severity:** medium
**Description.** Rewinding the grid to an earlier entry means replaying to that point and continuing from there; the run's original future (later journal events, later mutations) no longer describes the run. Keeping those entries in an append-only list would make later mutations regenerate identically and appear twice, and a replayed journal would re-apply abandoned brush strokes at their old generations. The two requirements conflict.
**Reproduction.** Rewind the grid to entry 3 of a 10-entry run and step: with an untruncated lineage, entry 4's rule reappears as entry 11.
**Notes.** Resolved: grid rewind truncates journal and lineage to the target point (time travel); rule-only rewind appends and keeps everything. SPEC §11 states this.

### BUG-007: GpuStepper's move constructor dropped fields added after it was written
**Status:** fixed
**Found:** 2026-09-12 (Phase 2, the session replay test)
**Location:** `src/sim/gpu_step.cpp`
**Severity:** high
**Description.** `Simulation::create` returns by value, so its `GpuStepper` is moved. The hand-written move constructor listed members by name and never learned about `mutation_` or the five uniform locations added later. In a moved stepper the locations were −1, `rlSetUniform` ignored them silently, and the threshold stayed 0: the GPU path of every `Simulation` ran without cell mutation while the CPU path mutated. The stepper-level equivalence tests never move a stepper and could not see it; the Simulation-level test that should have caught it earlier happened to compare GPU against GPU on the checks that mattered.
**Reproduction.** Create a `Simulation` on each path, set `p = 0.05`, step once, compare.
**Notes.** State is now split into `Owned` (GL handles, exchanged on move) and `Config` (plain data, copied wholesale), so a new field cannot be forgotten. `tests/sim/simulation_test.cpp` has a moved-stepper regression case.

## Won't Fix

*None.*

## Deferred

*None.*
