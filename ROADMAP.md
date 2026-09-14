# Roadmap

Phases are append-only. Mark Complete with an ISO date; do not delete.

## Phase 0 — Scaffolding
**Goal:** Repository exists with the documentation set and a build that compiles an empty window.
**Status:** Complete 2026-09-11
**Features delivered:** none
**Deliverables:**
- [x] Documentation scaffold (README, FEATURES, ROADMAP, ARCHITECTURE, DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, CLAUDE)
- [x] Licence selected (Apache-2.0) and `LICENSE` written (2026-09-11)
- [x] GitHub repository created under `Darian-Frey/` (2026-09-11)
- [x] Source tree laid out per README §Project structure (2026-09-11)
- [x] CMake project building raylib + rlImGui to a blank window (2026-09-11); GL 4.3 compute dispatch verified on both the Intel iGPU and the T1200
- [x] `BUILD.md` written at first successful build (2026-09-11)
**Acceptance:** `cmake --build build` produces a binary that opens a window and exits cleanly.

## Phase 1 — 2D discrete core
**Goal:** A working 2D automaton with declarative rules, a lookup-table backend, and a drawing canvas.
**Status:** Complete 2026-09-11
**Features delivered:** F-001, F-002, F-003, F-007, F-011, F-013, F-014, F-018
**Deliverables:**
- [x] `core/` grid and ping-pong texture pair, host and GPU, with the VRAM guard (2026-09-11)
- [x] `rule/` IR with validation and hash; neighbourhood enumeration; table layout (2026-09-11)
- [x] `rule/` DSL parser producing IR — B/S, B/S/C and count-condition table blocks (2026-09-11)
- [x] `rule/` lookup-table backend — `LutRule`, `selectBackend` (2026-09-11)
- [x] `sim/` accumulator-driven scheduler with step, pause, burst, per-frame cap and wall-clock budget (AV-003); `Simulation` owning grid, rule, both paths and the step/swap sequence (2026-09-11)
- [x] CPU reference stepper, all table kinds, all three boundaries, 1D/2D/3D; 28 gen/s at 1024² Life against a budget of 5 (2026-09-11). Runtime flag arrives with the scheduler.
- [x] Compute shader for the LUT execution path — `shaders/lut_step.comp`, specialised per rule shape and cached; a table change is a buffer upload (2026-09-11)
- [x] Painting canvas and random fill — `ui/` canvas paints through `Simulation::paintSpan` with no readback; fill via stream A (2026-09-11)
- [x] Palette rendering with pan and zoom — `render/renderer2d` + `View2D`, pixel-exact at integer zoom, tested against a render texture (2026-09-11); mouse/keyboard binding arrives with `ui/`
- [x] Equivalence test harness: CPU vs GPU, 1000 generations, bitwise — 15 fixture rules × 3 boundaries across 1D/2D/3D, passing on the Intel iGPU and the T1200 (2026-09-11)
**Acceptance:** Conway's Life, HighLife, Brian's Brain and a cyclic CA all run correctly at 1024² and ≥ 200 gen/s, with CPU and GPU agreeing bit-for-bit.
**Acceptance run (2026-09-11, T1200):** Life, Brian's Brain and the 8-state cyclic CA run in the application at 1024² at a 2,000 gen/s target, achieved, with the display at 60 fps; all four acceptance rules are in the CPU/GPU equivalence fixture set.
**Throughput figures (2026-09-11, T1200):** Life 1024² 3,684 gen/s; 2048² 1,210 gen/s; Brian's Brain 1024² 4,233 gen/s; 3D B5/S45 256³ 69 gen/s. Intel iGPU: 198 gen/s at 1024², 4 gen/s at 256³.

## Phase 2 — Mutation, lineage, sessions
**Goal:** The two mutation controls, the lineage log that makes them useful, and reproducible sessions.
**Status:** Complete 2026-09-12
**Features delivered:** F-015, F-016, F-017, F-020, F-023 (added 2026-09-11, D-012)
**Deliverables:**
- [x] Dual RNG streams — stream A PCG32 (CPU), stream B stateless hash in C++ and GLSL with a million-input agreement test (2026-09-11)
- [x] Cell mutation inside the compute step and the CPU oracle, equivalence suite extended to `p > 0`; UI control and `--seed-b` (2026-09-11)
- [x] Rule mutation operating on the IR, with recompile and invariant validation — validate-or-redraw ×8, million-edit fuzz, UI controls and `--rule-mutation` (2026-09-11)
- [x] Lineage log with pin and rewind — every rule change appends; rewind restores the rule and records itself; browser in the UI (2026-09-11). Grid rewind (replay to an entry's generation) arrives with sessions.
- [x] Session save/load with format version — JSON with journal, lineage deltas, sidecar for big grids; `--load`, Save/Load/Verify in the UI (2026-09-12)
- [x] Replay determinism test: `replay.*` CTest records 5000 generations under both mutations in one process, replays in fresh processes on each path, compares bitwise; in-process tests cover paints, fills, rule changes, rewinds and parameter changes (2026-09-12)
- [x] Grid rewind by replay, with journal and lineage truncated (2026-09-12)
- [x] Hexagonal lattice: `NeighbourhoodType::Hexagonal` on axial storage, `neighbourhood hex r`, hex fragment path and `View2D` hex mode with cube rounding, hex brush, three hexagonal equivalence fixtures (2026-09-12)
**Acceptance:** A session with both mutations active replays to a bit-identical grid on a fresh process, and any rule seen during the run can be recovered from the lineage log. A hexagonal Life-like rule runs on both paths and renders as a hex tiling.
**Acceptance run (2026-09-12):** `ctest -R replay` replays 5000 generations under both mutations bit-identically in fresh processes on both paths; every lineage entry is pinnable and rewindable; hex B2/S34 runs on both paths (equivalence fixtures) and renders as a hex tiling.

## Phase 3 — Three dimensions
**Goal:** The same engine on cubic lattices, with volume rendering.
**Status:** Complete 2026-09-12
**Features delivered:** F-004, F-019
**Deliverables:**
- [x] 3D texture grid path and 3D neighbourhood gathering (delivered in Phase 1: `GpuGrid` 3D textures, `lut_step.comp` 3D variant, 3D equivalence fixtures)
- [x] Volume raymarch renderer with per-state opacity — `render/renderer3d`, Amanatides–Woo voxel traversal, palette alpha as opacity, face shading (2026-09-12)
- [x] Orbit camera, clipping planes, slice view — `render/orbit`, View panel (2026-09-12)
- [x] Slice-based painting for 3D — `Orbit::pickOnSlab` → `paintSpan` with z (2026-09-12)
- [x] VRAM budget guard rejecting grid sizes that will not fit — `GpuGrid::create` (Phase 1); the Grid panel shows the footprint and the log carries the guard's message (2026-09-12)
**Acceptance:** A 3D life variant runs at 256³ and ≥ 30 fps within the 4 GB VRAM budget, with the same rule IR as its 2D counterpart where the rule family permits.
**Acceptance run (2026-09-12, T1200):** `B5/S45` at 256³ with the volume rendered every frame: 49 gen/s and 49 fps at a 60 gen/s target; 33.5 MB of VRAM for the pair. The rule is the same `outer_totalistic` IR family as its 2D form, with N = 26.

## Phase 4 — Lua and codegen
**Goal:** Rules too exotic for the DSL, and the GLSL backend that large rules need.
**Status:** Not started
**Features delivered:** F-008, F-009, F-010, F-025, F-026
**Deliverables:**
- [x] Cell life cycle: `decay N;` as a front-end desugaring, tail palettes and tail-only age shading (F-025, D-014, 2026-09-14). Landed ahead of the rest of the phase because it needs neither Lua nor codegen; its tail length is capped until one of them arrives.
- [x] Correlated cell mutation in aligned blocks (F-026, D-015, 2026-09-14). Extends Phase 2's mutation work; landed here because it shares the step shader with the rest of this phase.
- [x] `signature_literal` syntax defined in SPEC §7 and parsed — elements in canonical order, `_` wildcards, `rot` for rotation-symmetric tables, mixable with count conditions (IMP-002, 2026-09-14)
- [ ] Bundled rule library per the F-010 list, each rule with palette and description
- [x] Sandboxed Lua front end emitting IR (F-008, 2026-09-14)
- [x] Instruction budget and abort path, plus a memory budget for scripts that fill memory rather than loop (2026-09-14)
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
**Features delivered:** F-005, F-012, F-021, F-022, F-024
**Deliverables:**
- [ ] Screensaver mode: fullscreen playlist of bundled rules with optional mutation, exits on input (F-024, added 2026-09-12)
- [ ] 1D elementary automata with space-time rendering
- [ ] RLE import with placement
- [ ] PNG and frame-sequence export
- [ ] Headless mode
- [ ] `BENCHMARKS.md` with baseline numbers for each acceptance target
**Acceptance:** A first tagged release with a populated rule library and reproducible benchmark figures.
