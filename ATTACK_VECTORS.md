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
**Detection.** Implemented 2026-09-15. `tests/sim/equivalence_test.cpp` compiles Conway's Life as a table and as an expression tree, runs both through both execution paths for 1000 generations under each boundary, and requires all four results to be identical — so neither the backend nor the path may change what a rule does. Expression fixtures, including the three-dimensional Moore rule that has no finite table, are in the CPU/GPU comparison alongside the table ones. Separately, a direct test that the C++ and GLSL implementations of the stream-B hash agree over a large input sweep, block-correlated decisions included (2026-09-14).
**Related decisions.** D-004 (two backends), D-011.

### AV-012 Rule mutation produces an invalid IR
**Severity:** Major
**Description.** A point edit can write a state index outside `0 … S-1`, or perturb an expression literal out of its valid range. An unvalidated mutated IR reaching a backend produces out-of-range table lookups — undefined behaviour on the CPU path, silent garbage sampling on the GPU path.
**Detection.** Implemented 2026-09-11. `sim::mutateRule` validates every candidate and redraws up to 8 times; `tests/sim/rule_mutation_test.cpp` walks 10⁶ table edits across the fixture set asserting validity, and an expression fixture (a 25-state Generations rule) exercises the redraw path on every run. The cap itself is exercised only in the sense that its exhaustion returns "no mutation" and is counted as a skip; no fixture reliably produces eight invalid draws in a row.
**Related decisions.** D-002 (IR as single target), D-005.

### AV-016 Malformed or hostile pattern file
**Severity:** Major
**Description.** *(Phase 6.)* A pattern file is external data reaching the engine, as a session file is and as nothing the DSL or Lua front ends produce is. An RLE run-length header can claim an extent no allocation can satisfy, a native pattern can name a state index the grid's rule does not have or a lattice the grid is not, and either can stop halfway through a run. The failures that follow are an out-of-range write into the grid, an allocation sized from a number the file chose, and a half-placed pattern left behind by a parse that gave up part-way.
**Detection.** Not implemented (feature not yet built). Planned: the parser computes the pattern's extent before allocating anything, as AV-010 requires of tables; placement is all-or-nothing against an already validated pattern, as AV-014 requires of rule compilation; a fixture set of truncated, oversized and out-of-range files is expected to be refused with a diagnostic rather than to crash or to place part of itself.
**Related decisions.** D-017 (pattern formats), D-013 (journal), F-012.

### AV-017 Inspector explains a transition the engine did not perform
**Severity:** Major
**Description.** *(Phase 6.)* The cell inspector (F-030) exists to be believed: it is read at exactly the moments the user cannot work the answer out unaided. An inspector that derives the transition in its own code is a second implementation of SPEC §5's index arithmetic and SPEC §6's evaluation rules, free to drift from the one the engine runs. This is worse than a divergent backend, which betrays itself by producing visibly wrong automata; a divergent inspector produces confident, plausible prose about a cell, and the user has no way to check it. Boundary handling is the likeliest place to drift: an inspector that resolves neighbours differently from `sim::resolve` explains every interior cell correctly and lies about every cell on the rim.
**Detection.** Not implemented (feature not yet built). Planned: structural — the inspector calls the per-cell entry point the stepper itself is a loop over (IMP-005), so there is one implementation rather than two. Test: over every equivalence fixture and every boundary mode, the inspector's predicted next state equals the state `cpuStep` writes, for every cell of the grid including edges and corners.
**Related decisions.** D-018 (the inspector is the oracle), D-011 (CPU reference as oracle), AV-005, AV-007.

### AV-018 Energy or resource created where nothing should create it
**Severity:** Major
**Description.** *(Post Phase 5.)* Once a field carries a quantity that is meant to be conserved — the resource of F-032, or any energy budget built on it — every arithmetic path that touches it is a place the quantity can be created from nothing. A clamp applied in the wrong order, a regeneration step that runs before consumption instead of after, a share that rounds up: each is a leak. What makes this worse than an ordinary numerical bug is that the grid is under selection. A rule lineage that happens to exploit the leak outbreeds every lineage that does not, so the defect does not sit quietly producing slightly wrong totals — it takes over the grid, and the first symptom is a population that thrives for no visible reason.
**Detection.** Not implemented (feature not yet built). Planned: the resource field's only sources are regeneration and its only sinks are consumption and decay, each counted; F-036's reduction reports the total against what entered and left, and a test runs a fixture for a thousand generations asserting the books balance to within float tolerance. A sudden divergence between the counted total and the measured one localises the leak to the step that opened it.
**Related decisions.** D-019 (gather boundary), F-032, F-036.

---

## Rule authoring

### AV-008 Lua reaching a per-cell path
**Severity:** Critical
**Description.** The compile-time-only invariant (D-003) is architecturally load-bearing and socially fragile. A future feature request — "let the rule read a user variable that changes each generation" — has an obvious implementation that involves calling into Lua from the step loop, and that implementation would be several thousand times too slow and impossible on the GPU.
**Detection.** Structural, and now enforced by construction (2026-09-14): `rule::compileLua` creates the `lua_State` in a local RAII object and returns a plain `RuleIR`, so no Lua handle exists outside the call and there is nothing for a step loop to call into. The chunk also runs with its own environment table rather than the real globals, so a script cannot stash anything either. `tests/rule/lua_test.cpp` checks the observable half: a global set by one script is gone by the next compile. Any change that lets a `lua_State` escape `rule/lua` is a review flag.
**Related decisions.** D-003.

### AV-009 Pathological Lua script at compile time
**Severity:** Major
**Description.** A user rule script containing an unbounded loop hangs the application at rule-compile time with no way out. Since scripts are expected to do real work — a script may legitimately compute a large transition table exhaustively — a naive timeout would also kill valid scripts.
**Detection.** Implemented 2026-09-14. A count hook enforces `LUA_INSTRUCTION_BUDGET` and a capped allocator enforces `LUA_MEMORY_BUDGET`; both abort with the budget named, and the running rule is untouched like any other failed compile. `tests/rule/lua_test.cpp` covers an endless loop, a script that fills memory without looping, and a script doing real work well inside its budget. The instruction budget is counted rather than timed, so the abort point does not vary with machine speed. The memory budget was added because the instruction budget alone does not bound a script that builds a table.
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
**Detection.** Partly implemented 2026-09-15. The shader cache keyed on `ir_hash` exists, and a test checks that a repeated rule does not compile twice while a table rule of unchanged shape never compiles at all. Measured on the T1200, generating and compiling a 16-state expression rule costs 61 ms on a session's first compile and under 2 ms after, so the SPEC §12 budget of 250 ms holds with room; that is a measurement rather than an assertion, because a timing assertion in the suite would be flaky on a cold driver. Still not implemented: surfacing compile time in the UI and warning when it exceeds the mutation interval.
**Related decisions.** D-004.

---

## Numerical

### AV-015 Float precision divergence in continuous automata
**Severity:** Major
**Description.** *(Phase 5.)* Continuous automata accumulate float error over thousands of generations. Different GPUs, drivers, and optimisation levels may reassociate arithmetic or contract multiply-add differently, so identical sessions diverge across machines — breaking the SPEC §11 determinism contract specifically for `f32` rules.
**Detection.** Partly implemented 2026-09-17; see BUG-011 for what it does not yet cover. `tests/sim/continuous_test.cpp` steps a kernel rule 1000 generations on both paths under every boundary and both growth forms, with cell mutation on, and requires bitwise equality. It was red when first written and three separate causes had to be removed:

1. The oracle accumulated the convolution in `double` while the shader could only manage `float`. More accurate, and therefore wrong: the two must agree before either is precise. The CPU now accumulates in `float`, in offset order.
2. The shader contracted `a*b+c` into an fma, which is a different result from a multiply followed by an add. `precise` on the convolution and on every float temporary of a generated growth function forbids the contraction and the reassociation that goes with it.
3. The polynomial growth divided by `9σ²`. GLSL permits float division 2.5 ULP of error where C++ is correctly rounded, so a divide is a guaranteed disagreement. `rule/growth` now computes the reciprocal on the host and emits a multiply, which both sides round identically.

The third is the general lesson: **generated float code must not divide.** The first is the general trap: the safe-looking instinct to accumulate in a wider type is what broke it.

With all three fixed the paths agree bitwise over 1000 generations on both the Intel iGPU (Mesa) and the T1200 (NVIDIA), at the sizes the suite exercises, and a continuous rule at 256² matches the CPU path on both drivers.

**Corrected 2026-09-17.** This entry first claimed a 128² configuration run to 10,000 generations on each path and compared identical. It did — but the rule used there dies out, so the comparison was of two empty grids and carried no evidence at all. A run that ends empty proves nothing, and the check should have been the mass before the comparison. The claim is withdrawn.

What replaced it is worse news: at 512² a continuous rule goes wrong on both GPUs and differently on each, and NVIDIA is not even reproducible against itself. BUG-011 has the detail. So the determinism contract holds for `f32` at the sizes tested here and **not** at the size SPEC §12 asks for, and the arithmetic findings above stand while the conclusion drawn from them does not extend to 512².
**Related decisions.** D-006 (determinism contract), D-010 (continuous states in IR from v1), D-020 (what a continuous rule means).
