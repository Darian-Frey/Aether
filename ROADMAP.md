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
**Status:** Complete 2026-09-15
**Features delivered:** F-008, F-009, F-010, F-025, F-026
**Deliverables:**
- [x] Cell life cycle: `decay N;` as a front-end desugaring, tail palettes and tail-only age shading (F-025, D-014, 2026-09-14). Landed ahead of the rest of the phase because it needs neither Lua nor codegen; its tail length is capped until one of them arrives.
- [x] Correlated cell mutation in aligned blocks (F-026, D-015, 2026-09-14). Extends Phase 2's mutation work; landed here because it shares the step shader with the rest of this phase.
- [x] `signature_literal` syntax defined in SPEC §7 and parsed — elements in canonical order, `_` wildcards, `rot` for rotation-symmetric tables, mixable with count conditions (IMP-002, 2026-09-14)
- [x] Counted-set indexing (IMP-001, D-016): `counted_totalistic`, which lifted the table-size ceiling on ageing tails, Generations rules and the cyclic CA (2026-09-14)
- [x] Bundled rule library per the F-010 list, each rule with palette and description — fourteen rules in `rules/`, `--rule @id`, Library panel, user rules saved back (2026-09-14). Langton's loops and the 1D rules remain, each for a stated reason.
- [x] Sandboxed Lua front end emitting IR (F-008, 2026-09-14)
- [x] Instruction budget and abort path, plus a memory budget for scripts that fill memory rather than loop (2026-09-14)
- [x] GLSL codegen backend with template and shader cache — `rule/glsl` generates `aether_rule`, the step shader calls it, the cache is keyed on `ir_hash` (2026-09-15)
- [x] Automatic backend selection from IR shape — `selectBackend`, reported in the UI as metadata and never a choice (2026-09-15)
- [x] Backend equivalence test: rules expressible both ways compiled through both, compared (AV-007, 2026-09-15)
- [x] `Expression` interpreter on the CPU path, so the oracle covers every rule the GPU runs (2026-09-15)
**Acceptance:** A non-totalistic 3D Moore rule that cannot fit a lookup table runs correctly via generated GLSL, and every rule expressible through both backends produces identical output.
**Acceptance run (2026-09-15, T1200):** a 3D Moore rule reading individual neighbours — 2²⁶ entries per state as a table — runs through codegen and agrees with the CPU oracle bitwise over 1000 generations under all three boundaries; Life compiled as a table and as an expression gives identical grids on both paths. Generating and compiling a rule costs 61 ms cold and under 2 ms warm against a 250 ms budget, and a generated 16-state rule steps 1024² at ~2,900 gen/s.

## Phase 5 — Continuous states
**Goal:** Float-state automata sharing the existing pipeline.
**Status:** Complete 2026-09-19
**Features delivered:** F-006
**Deliverables:**
- [x] Float texture grid path (2026-09-16): `HostGrid` keeps byte storage with float views over it, the session codec and sidecar work in bytes rather than cells, and `Simulation::create` refuses a grid whose cell type disagrees with its rule's
- [x] Convolution kernel authoring, radial and explicit (2026-09-16): Lua returns a profile it computes itself, so the shell arrives sampled
- [x] Growth function in the IR expression tree (2026-09-16): named forms — rectangular and polynomial — lowered by `rule/growth`; the Gaussian is deliberately absent, needing `exp`, which SPEC §6 forbids relying on
- [x] The continuous step on the CPU path (2026-09-17): convolve, grow, clamp; `rule/kernel` resolves a profile onto the offsets and normalises it (D-020)
- [x] The continuous step on the GPU path (2026-09-17): `aether_rule_f` generated from the growth expression, `continuous_step.comp` doing the convolution, and CPU/GPU equivalence bitwise over 1000 generations under every boundary
- [x] Documented precision expectations across GPU vendors (2026-09-17, AV-015): `precise` float temporaries, no division in generated float code, and the oracle accumulating in `float` rather than `double`. Mesa and the T1200 are bitwise identical at 512² over 1000 generations
- [x] `f32` cells through the palette (2026-09-17): a float variant of the palette pass, a monotone ramp rather than the hue cycle a state count gets, and `fill`, `paint` and the readouts taught what a float cell is
**Acceptance:** A basic Lenia configuration runs stably at 512² without state divergence over 10,000 generations. **Met** 2026-09-19: `rules/lenia.lua` holds a 28% field at 512² over 10,000 generations on the T1200, the two drivers are bitwise identical there over 1000, and the CPU path agrees — bitwise at 128² over 2500 and by trajectory at 512² over 4600. SmoothLife was dropped from this acceptance by D-021: it is a function of two convolutions where a `Kernel` carries one profile, which makes it a second shape of rule rather than a second example of this one. It is a candidate, with its cost recorded.

## Phase 6 — Presentation and release
**Goal:** The things that make it pleasant rather than merely correct.
**Status:** Complete 2026-09-26
**Features delivered:** F-005, F-012, F-021, F-022, F-024, F-027, F-028, F-029, F-030
**Deliverables:**
- [x] Screensaver mode: fullscreen playlist of bundled rules with optional mutation, exits on input (F-024, added 2026-09-12; complete 2026-09-23). `ui/screensaver` is a deterministic playlist, so what it showed can be found again; each entry prints the command that reproduces it. The `XSCREENSAVER_WINDOW` half was conditional on proving practical and does not — raylib makes its own window and cannot adopt one
- [x] 1D elementary automata with space-time rendering (F-005, complete 2026-09-23). `W110` in the DSL, the table built through the layout's own index function; `render/spacetime` keeps the history as a ring written by a GPU-to-GPU copy, one row per generation, drawn as two bands so the seam costs nothing
- [x] Pattern import and export (F-012, widened 2026-09-15 by D-017; complete 2026-09-21). `sim/pattern` and SPEC §14 carry Golly's extended RLE and a native `.pattern`; `Simulation::placePattern`/`extractPattern` move a pattern in and a region out, the former journalled as a `place` event so a session replays the paste; a Patterns panel opens a file and places it by cursor with a preview drawn rather than written, and shift-drag selects a region to write back out
- [x] Bundled pattern library in `patterns/`, listed and placeable the way the rule library is (F-027, complete 2026-09-21). Eight patterns, each verified by running it: three transcribed Life patterns, and a Wireworld loop, Brian's Brain glider, cyclic spiral seed, hex oscillator and 3D Bays shell constructed here. A pattern the running rule cannot take is greyed in the list rather than failing at the click
- [x] Region seeding: seed a dragged rectangle at the fill densities (F-028, added 2026-09-15; complete 2026-09-21), and lift the whole-grid Seed control out from under the density sliders (IMP-004, applied with it). `sim::fillRandomRegion` is the one implementation and the whole-grid fill is it over the whole extent, so stream A is consumed as it always was and older sessions replay unchanged; a `fill_region` journal event carries the bounds. 3D seeds the painting slice, there being no gesture that drags a rectangle there
- [x] Pattern editor: a host-side scratch pad painted a cell at a time, stepping forward and back independently of the live run, saving to `patterns/` (F-029, added 2026-09-15 by D-018; complete 2026-09-22). `sim::Scratch` holds a grid and a rule of its own and touches no GL, so it is tested without a display; a floating window draws it and paints it with the F-011 brush. Its contents leave as an ordinary pattern, placed through F-012 or saved into `patterns/`
- [x] Cell inspector: the state, neighbours, counts and rule clause behind one cell's transition, taken from the oracle's own per-cell entry point rather than derived a second time (F-030, AV-017, complete 2026-09-22). `sim::inspect` calls `stepCell` and reads its working, including which conditional answered, which the arena already records. Asserted against the stepper over every equivalence fixture, boundary and cell
- [x] PNG and frame-sequence export (F-021, complete 2026-09-23). An Export section writes the viewport — the automaton, not the panels — as a PNG, or as a numbered sequence over a generation range. A recording steps by generations rather than by frames, so the sequence is the run and not the machine's frame rate; `ui/capture` holds that arithmetic and is tested without a display
- [x] Headless mode (F-022, complete 2026-09-23). `aether headless --png FILE` dumps the final grid and `--frame-dir DIR` a numbered sequence, through the same renderer and palette pass the window uses rather than a second host-side mapping of states to colours. 3D is refused rather than guessed at, a volume needing a camera. The `frames.*` CTest cases check the sequence's arithmetic against the run's own final state
- [x] `BENCHMARKS.md` with baseline numbers for each acceptance target (complete 2026-09-25). Every SPEC §12 threshold measured and met on the T1200, with the method, the 4% noise floor and `scripts/benchmark.sh` to reproduce them; the integrated GPU is recorded for contrast and misses the 3D target at 6 gen/s against 30
**Acceptance:** A first tagged release with a populated rule library, a populated pattern library and reproducible benchmark figures. **Met** 2026-09-26 as `v0.1.0`: nineteen bundled rules and nine bundled patterns, each checked by running it, and `BENCHMARKS.md` with every SPEC §12 threshold measured on the T1200 and `scripts/benchmark.sh` to take them again.
F-002 was closed in the same pass. Its third acceptance point — bit-identical grids for every rule *in the bundled library* — had been outstanding since Phase 1 behind the note "Remaining: the bundled library as the fixture set (Phase 4)", and Phase 4 came and went without it. The equivalence suite now sweeps all eighteen discrete bundled rules under three boundaries, with and without cell mutation, on both GPUs. It passes, which means nothing was hiding; it was checked because the register said it had not been.

## Phase 7 — Ecosystem
**Goal:** Cells that inherit a rule, compete for a resource and are selected rather than merely mutated.
**Status:** Not started
**Features delivered:** F-031, F-032, F-033, F-034, F-035, F-036
**Deliverables:**
- [ ] Multi-field grids: a site carries the state plus declared auxiliary fields, each its own texture, additive in the IR and the session format (F-031)
- [ ] Abiotic resource field: patchy noise seeding, regeneration toward a carrying capacity, optional diffusion, consumed at a cell's own site (F-032)
- [ ] Per-cell genome with inheritance at birth — majority, random parent or crossover — with per-gene mutation, all drawn from stream B (F-033)
- [ ] Hard cell lifespan alongside the soft decay of F-025 (F-034)
- [ ] Similarity-biased birth, the gather-compatible half of herding (F-035)
- [ ] Population and field readouts as GPU reductions, never a per-step readback (F-036, AV-018)
**Acceptance:** A genome sweeps a grid under selection and the sweep is visible in the population graph; a resource field's books balance over 1000 generations; and the whole run replays bit-identically from its seed, inheritance and all.
**Notes:** Added 2026-09-16 by D-019, from `docs/ecosystem-design-note.md`. Placed after the release phase rather than inside it, and after Phase 5 because the resource field is a second `f32` field and inherits that work. The design note's feeding, movement and clan energy sharing are not here; D-019 records the boundary and the two routes back.
