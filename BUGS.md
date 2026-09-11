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

*None.*

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

## Won't Fix

*None.*

## Deferred

*None.*
