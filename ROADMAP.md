# Roadmap

Phases are append-only. Mark Complete with an ISO date; do not delete.

## Phase 0 — Scaffolding
**Goal:** Repository exists with the documentation set and a build that compiles an empty window.
**Status:** In progress
**Features delivered:** none
**Deliverables:**
- [x] Documentation scaffold (README, FEATURES, ROADMAP, ARCHITECTURE, DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, CLAUDE)
- [x] Licence selected (Apache-2.0) and `LICENSE` written (2026-09-11)
- [x] GitHub repository created under `Darian-Frey/` (2026-09-11)
- [x] Source tree laid out per README §Project structure (2026-09-11)
- [ ] CMake project building raylib + rlImGui to a blank window
- [ ] `BUILD.md` written at first successful build
**Acceptance:** `cmake --build build` produces a binary that opens a window and exits cleanly.

## Phase 1 — 2D discrete core
**Goal:** A working 2D automaton with declarative rules, a lookup-table backend, and a drawing canvas.
**Status:** Not started
**Features delivered:** F-001, F-002, F-003, F-007, F-011, F-013, F-014, F-018
**Deliverables:**
- [ ] `core/` grid and ping-pong texture pair
- [ ] `rule/` DSL parser producing IR; lookup-table backend
- [ ] `sim/` accumulator-driven scheduler with step, pause, burst
- [ ] CPU reference stepper behind a runtime flag
- [ ] Compute shader for the LUT execution path
- [ ] Painting canvas and random fill
- [ ] Palette rendering with pan and zoom
- [ ] Equivalence test harness: CPU vs GPU, 1000 generations, bitwise
**Acceptance:** Conway's Life, HighLife, Brian's Brain and a cyclic CA all run correctly at 1024² and ≥ 200 gen/s, with CPU and GPU agreeing bit-for-bit.

## Phase 2 — Mutation, lineage, sessions
**Goal:** The two mutation controls, the lineage log that makes them useful, and reproducible sessions.
**Status:** Not started
**Features delivered:** F-015, F-016, F-017, F-020
**Deliverables:**
- [ ] Dual RNG streams with a shared PCG32 implementation on CPU and GLSL
- [ ] Cell mutation inside the compute step
- [ ] Rule mutation operating on the IR, with recompile and invariant validation
- [ ] Lineage log with pin and rewind
- [ ] Session save/load with format version
- [ ] Replay determinism test: save at generation 0, replay 5000 generations, compare
**Acceptance:** A session with both mutations active replays to a bit-identical grid on a fresh process, and any rule seen during the run can be recovered from the lineage log.

## Phase 3 — Three dimensions
**Goal:** The same engine on cubic lattices, with volume rendering.
**Status:** Not started
**Features delivered:** F-004, F-019
**Deliverables:**
- [ ] 3D texture grid path and 3D neighbourhood gathering
- [ ] Volume raymarch renderer with per-state opacity
- [ ] Orbit camera, clipping planes, slice view
- [ ] Slice-based painting for 3D
- [ ] VRAM budget guard rejecting grid sizes that will not fit
**Acceptance:** A 3D life variant runs at 256³ and ≥ 30 fps within the 4 GB VRAM budget, with the same rule IR as its 2D counterpart where the rule family permits.

## Phase 4 — Lua and codegen
**Goal:** Rules too exotic for the DSL, and the GLSL backend that large rules need.
**Status:** Not started
**Features delivered:** F-008, F-009, F-010
**Deliverables:**
- [ ] Sandboxed Lua front end emitting IR
- [ ] Instruction budget and abort path
- [ ] GLSL codegen backend with template and shader cache
- [ ] Automatic backend selection from IR shape
- [ ] Backend equivalence test: rules expressible both ways compiled through both, compared
**Acceptance:** A non-totalistic 3D Moore rule that cannot fit a lookup table runs correctly via generated GLSL, and every rule expressible through both backends produces identical output.

## Phase 5 — Continuous states
**Goal:** Float-state automata sharing the existing pipeline.
**Status:** Not started
**Features delivered:** F-006
**Deliverables:**
- [ ] Float texture grid path
- [ ] Convolution kernel authoring, radial and explicit
- [ ] Growth function in the IR expression tree
- [ ] Documented precision expectations across GPU vendors
**Acceptance:** SmoothLife and a basic Lenia configuration run stably at 512² without state divergence over 10,000 generations.

## Phase 6 — Presentation and release
**Goal:** The things that make it pleasant rather than merely correct.
**Status:** Not started
**Features delivered:** F-005, F-012, F-021, F-022
**Deliverables:**
- [ ] 1D elementary automata with space-time rendering
- [ ] RLE import with placement
- [ ] PNG and frame-sequence export
- [ ] Headless mode
- [ ] `BENCHMARKS.md` with baseline numbers for each acceptance target
**Acceptance:** A first tagged release with a populated rule library and reproducible benchmark figures.
