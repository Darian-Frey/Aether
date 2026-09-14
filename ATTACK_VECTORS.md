# Attack Vectors

Project-specific failure modes Aether must be resilient against. Grouped by category; each vector states a detection method and a severity.

Severity: Critical (must hold) | Major (regression blocks release) | Minor (track only).

Detection may be automated, manual, or explicitly not implemented — the requirement is that it is *defined*. Vectors carrying `Detection: not implemented` are honest gaps, and the gap is itself signal.

---

## Resource limits

### AV-001 3D grid exceeds available VRAM
**Severity:** Critical
**Description.** Grid memory grows as the cube of the extent. On the 4 GB target GPU, a `u8` ping-pong pair at 512³ is 268 MB and fits; at 1024³ it is 2.1 GB and, with the renderer's own allocations, does not. An allocation attempted without a prior check either fails opaquely or succeeds into swap and destroys frame times. The user-facing symptom is indistinguishable from a hang.
**Detection.** Not implemented (would require the guard specified in SPEC §2). Planned: `sim/` computes the footprint from grid parameters before allocation, compares against queried available VRAM plus 25% headroom, and rejects with a message naming both figures. Test: attempt an over-budget allocation in headless mode and assert a clean rejection.
**Related decisions.** D-001 (GPU residency), D-008 (dense grid).
**History.** Identified during initial design, 2026-08-30, from the 4 GB budget on the target hardware.

### AV-002 Host–GPU synchronisation stall per generation
**Severity:** Major
**Description.** Any host read of grid state — a statistics panel showing live population, a naive screenshot path, a debug assertion — forces a pipeline flush and destroys the throughput that motivated D-001. This is easy to introduce accidentally and hard to notice, because the simulation still works, just slowly.
**Detection.** Not implemented (would require a frame-time regression test). Planned: benchmark harness asserting the SPEC §12 throughput targets; a sustained drop of more than 20% between commits fails. Manual: any new `glGetTexImage`, `glReadPixels` or buffer map in the step path is a review flag.
**Related decisions.** D-001.

### AV-003 Multi-step-per-frame starves rendering and input
**Severity:** Major
**Description.** The accumulator (F-014) runs as many generations per frame as the target rate demands. At a high target rate on a large grid, the step loop can consume the entire frame budget, leaving the UI unresponsive — including the control that would let the user lower the rate.
**Detection.** Implemented 2026-09-11. `sim::Scheduler` has a hard per-frame cap, a host-side wall-clock budget, and frame-time feedback: a frame longer than `slowFrame` (default 1/30 s) halves the effective cap, short frames let it recover. The feedback matters because GPU steps return in microseconds and their real cost surfaces only as a long frame — without it the Intel iGPU dropped to 5 fps at an unreachable target; with it, 41 fps. The shortfall is shown in the UI header as "below target" and the effective cap next to the slider. Tests in `tests/sim/scheduler_test.cpp`.
**Related decisions.** D-001.

### AV-010 Lookup table size computed after allocation
**Severity:** Critical
**Description.** Table size grows combinatorially with state count and neighbourhood size (SPEC §5). A non-totalistic 3D Moore binary rule needs 1.3×10⁸ entries. If the compiler builds the table and then checks whether it fits, the check never runs — the allocation has already exhausted memory. The failure mode is an out-of-memory abort during what looks like an innocuous rule change.
**Detection.** Not implemented. Planned: unit test asserting that the compiler routes each rule in a fixture set to the correct backend, including several rules just above and just below `LUT_MAX_ENTRIES`, with an allocation counter asserting no table allocation occurs on the codegen path.
**Related decisions.** D-004 (backend threshold).

---

## Correctness

### AV-004 Ping-pong buffer aliasing
**Severity:** Critical
**Description.** If the compute step reads and writes the same texture, cells see a mixture of current-generation and next-generation neighbours depending on scheduling. The result still looks like a cellular automaton — it is just a different, non-deterministic one. This is the single most likely way to produce output that is wrong but plausible.
**Detection.** Structural: the swap occurs in exactly one place, and the step function takes read and write handles as distinct parameters, so aliasing requires passing the same handle twice. Test: a known-pattern test — a glider traversing the grid for 100 generations and arriving at its exact predicted offset — fails immediately under aliasing.
**Related decisions.** D-001.
**Related claims.** —

### AV-005 Boundary condition divergence between execution paths
**Severity:** Critical
**Description.** Boundary handling is easy to implement differently in a shader (where wrapping may fall out of the sampler's address mode) and in C++ (where it is written explicitly). Interior cells agree, edge cells do not, and on a wrapped grid the disagreement propagates inward over subsequent generations.
**Detection.** Not implemented (requires F-002 to exist). Planned: CPU/GPU equivalence test over 1000 generations on a grid deliberately seeded with activity at all edges and corners, run once per boundary mode, asserting bitwise equality.
**Related decisions.** D-011 (CPU reference as oracle).

### AV-006 Non-reproducible runs from unseeded or ambient randomness
**Severity:** Critical
**Description.** A single `rand()`, a time-derived seed, or a thread-index-derived value anywhere in the step path makes the session record (SPEC §11) a lie: the file claims reproducibility it cannot deliver, and the user discovers this only when a saved result fails to replay — typically the one result worth keeping.
**Detection.** Implemented 2026-09-12. CTest `replay.record` runs 5000 generations with both mutations active in one process and saves; `replay.gpu` and `replay.cpu` replay the file from its initial state in fresh processes on each path; `replay.compare_*` assert bitwise equality of the grids and the lineage hashes. In-process tests (`tests/sim/session_test.cpp`) do the same with paints, fills, rule changes, rewinds and parameter changes in the journal. Still manual: a grep for prohibited RNG sources (SPEC §10) in `sim/` and `shaders/`.
**Related decisions.** D-005 (dual streams), D-006 (reproducibility quadruple).

### AV-007 Backend divergence on the same rule
**Severity:** Critical
**Description.** A rule near the size threshold may be expressible through both the table and codegen backends. If the two produce different results, behaviour depends on `LUT_MAX_ENTRIES` — meaning a tuning constant silently changes simulation semantics. The same class of divergence applies between CPU and GPU implementations of the stream-B hash (SPEC §10).
**Detection.** Not implemented (requires Phase 4). Planned: for every rule in the fixture set expressible both ways, compile through both backends and compare 1000 generations bitwise. Separately, a direct test that the C++ and GLSL implementations of the stream-B hash agree over a large input sweep, block-correlated decisions included (2026-09-14).
**Related decisions.** D-004 (two backends), D-011.

### AV-012 Rule mutation produces an invalid IR
**Severity:** Major
**Description.** A point edit can write a state index outside `0 … S-1`, or perturb an expression literal out of its valid range. An unvalidated mutated IR reaching a backend produces out-of-range table lookups — undefined behaviour on the CPU path, silent garbage sampling on the GPU path.
**Detection.** Implemented 2026-09-11. `sim::mutateRule` validates every candidate and redraws up to 8 times; `tests/sim/rule_mutation_test.cpp` walks 10⁶ table edits across the fixture set asserting validity, and an expression fixture (a 25-state Generations rule) exercises the redraw path on every run. The cap itself is exercised only in the sense that its exhaustion returns "no mutation" and is counted as a skip; no fixture reliably produces eight invalid draws in a row.
**Related decisions.** D-002 (IR as single target), D-005.

---

## Rule authoring

### AV-008 Lua reaching a per-cell path
**Severity:** Critical
**Description.** The compile-time-only invariant (D-003) is architecturally load-bearing and socially fragile. A future feature request — "let the rule read a user variable that changes each generation" — has an obvious implementation that involves calling into Lua from the step loop, and that implementation would be several thousand times too slow and impossible on the GPU.
**Detection.** Manual/structural: the Lua interpreter handle is owned by `rule/lua` and is not reachable from `sim/` or `render/` by construction. Any change that widens its visibility is a review flag. Planned automated check: assert the Lua state is destroyed after compilation completes, so that a step-loop call would fault rather than merely be slow.
**Related decisions.** D-003.

### AV-009 Pathological Lua script at compile time
**Severity:** Major
**Description.** A user rule script containing an unbounded loop hangs the application at rule-compile time with no way out. Since scripts are expected to do real work — a script may legitimately compute a large transition table exhaustively — a naive timeout would also kill valid scripts.
**Detection.** Not implemented. Planned: a Lua debug hook enforcing `LUA_INSTRUCTION_BUDGET` (SPEC §8), tested with a fixture script containing an infinite loop, asserting a clean abort with the budget named in the diagnostic. The budget is instruction-counted rather than wall-clock so that the abort point is deterministic and does not vary with machine speed.
**Related decisions.** D-003.

### AV-014 Rule compilation failure leaves the engine in a half-updated state
**Severity:** Major
**Description.** A failed compile that has already replaced the current table, or updated grid parameters to match the new rule's state count, leaves the simulation running against a rule that does not exist. The user sees corruption and has no way to attribute it to the syntax error they just made.
**Detection.** Not implemented. Planned: compilation builds a complete new compiled-rule object and swaps it in only on success; failure paths touch no engine state. Test: submit a series of malformed rules while a simulation runs, asserting the grid evolves identically to a control run with no submissions.
**Related decisions.** D-002.

---

## Evolutionary dynamics

### AV-011 Mutation drift into degenerate rules with no recovery path
**Severity:** Major
**Description.** Most of rule space is uninteresting — rules that fill the grid, empty it, or freeze. A random walk under F-015 will spend most of its time there. Without the lineage log, a run that passes through something remarkable and then drifts into a fixed point has lost it permanently, and the user's only recourse is to start again and hope.
**Detection.** Manual: this is a design-level vector rather than a testable defect. The mitigation is F-017 (lineage log with pin and rewind), which is a Must feature specifically because of this vector. Planned automated component: a degeneracy indicator in the UI — population fraction and change rate over a sliding window — so that a stalled run is visibly stalled rather than apparently paused.
**Related decisions.** D-005.
**History.** Identified during initial design, 2026-08-30. This vector is the reason F-017 is Must rather than Should.

### AV-013 Mutation interval shorter than codegen compile time
**Severity:** Minor
**Description.** A rule on the codegen backend with a short mutation interval recompiles a shader every few generations. On a cache miss this is hundreds of milliseconds, so the simulation spends most of its time compiling rather than stepping, presenting as a severe unexplained slowdown that appears only for large rules.
**Detection.** Not implemented. Planned: the shader cache keyed on `ir_hash` (SPEC §6) makes repeat rules free, but a random walk mostly produces novel rules. Mitigation is to surface compile time in the UI and warn when it exceeds the mutation interval. Test: measure compile time on the codegen fixture set and assert the SPEC §12 budget.
**Related decisions.** D-004.

---

## Numerical

### AV-015 Float precision divergence in continuous automata
**Severity:** Major
**Description.** *(Phase 5.)* Continuous automata accumulate float error over thousands of generations. Different GPUs, drivers, and optimisation levels may reassociate arithmetic or contract multiply-add differently, so identical sessions diverge across machines — breaking the SPEC §11 determinism contract specifically for `f32` rules.
**Detection.** Not implemented (feature not yet built). Planned: generated GLSL for the continuous path forbids reliance on driver-dependent contraction (SPEC §6); a cross-machine replay comparison at 10,000 generations establishes whether the contract holds in practice. If it does not, the honest response is to narrow the determinism claim for `f32` rather than to claim a guarantee the hardware does not provide.
**Related decisions.** D-006 (determinism contract), D-010 (continuous states in IR from v1).
