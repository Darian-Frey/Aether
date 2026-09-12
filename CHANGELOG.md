# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com). Entries reference stable IDs (`F-`, `D-`, `AV-`, `BUG-`, `IMP-`) where applicable.

## [Unreleased]

### Added
- 3D presentation (F-019): `render/renderer3d` with `shaders/volume.frag` (Amanatides–Woo traversal, palette alpha as opacity, face shading), `render/orbit` camera with slab picking; View panel with opacity, clip ranges, slice mode; slice painting; `--size WxHxD`, Depth in the Grid panel, headless 3D runs (2026-09-12).
- Hexagonal lattices (F-023, D-012): `NeighbourhoodType::Hexagonal` with axial offsets, `neighbourhood hex r` in the DSL, `View2D` hex mode (cube rounding, rhombus fit), hex path in `palette2d.frag`, hex-shaped brush, hex equivalence fixtures (2026-09-12).
- Sessions (F-020): `sim/session` — `.aether` JSON per SPEC §11 with a journal of every user action, delta-encoded lineage, a raw sidecar for grids over 4M cells; `Simulation::session/resume/replay/rewindGrid`; `rule/ir_json` with base64 (2026-09-12).
- Headless subcommands `aether headless | replay | compare` and the cross-process `replay.*` CTest cases (AV-006 detection implemented) (2026-09-12).
- UI Session section (path, Save, Load, Verify replay), `grid` rewind on lineage entries, `--load FILE` (2026-09-12).
- nlohmann/json `v3.12.0` as a dependency (2026-09-12).
- Rule mutation (F-015): `sim/rule_mutation` with class-preserving point edits on tables and expressions, validate-or-redraw, million-edit fuzz; fired from `Simulation::step` on the interval (2026-09-11).
- Lineage log (F-017): `sim/lineage`, appended by every successful rule install; pin/unpin; `Simulation::rewind`; lineage browser and rule-mutation controls in the UI; `--rule-mutation N[:M]`, `--cell-mutation P` (2026-09-11).
- Scheduler frame-time feedback: the effective per-frame cap halves on a long frame and recovers on short ones (AV-003 detection implemented) (2026-09-11).
- Stream B: `sim/hash.hpp` and `shaders/hash.glsl`, the same `hash32`/`mix32`/`uniformState` in C++ and GLSL, with a GPU test comparing them over a million inputs (2026-09-11).
- Cell mutation (F-016) in `cpuStep` and `lut_step.comp`, driven by `CellMutation{threshold, seedB}`; `Simulation::setCellMutation(p)`; Mutation panel and `--seed-b` (2026-09-11).
- Documentation scaffold: README, FEATURES, ROADMAP, ARCHITECTURE, DECISIONS, SPEC, ATTACK_VECTORS, BUGS, IMPROVEMENTS, CHANGELOG, CLAUDE (2026-08-30).
- Feature register F-001 … F-022 covering engine, rule authoring, initial state, dynamics, presentation, and session handling.
- Decision register D-001 … D-011 covering execution backend, rule IR, Lua scoping, backend selection, mutation model, reproducibility, technology stack, grid representation, project name, continuous-state provision, and the CPU reference oracle.
- Attack vector register AV-001 … AV-015 across resource limits, correctness, rule authoring, evolutionary dynamics, and numerical stability.
- Technical specification covering the cell and grid model, neighbourhoods, rule IR schema and validation, lookup-table layout and backend threshold, GLSL codegen contract, DSL grammar, Lua sandbox and budget, mutation semantics, RNG streams, session format, performance budgets, and rendering.
- `core/`: `GridSpec` with footprint arithmetic, `PingPong<T>` as the single swap, `HostGrid` byte buffers, `GpuGrid` as `GL_R8UI` texture pairs (2D and 3D) with upload/download, `queryVram` via NVX/ATI extensions, and the SPEC §2 VRAM guard (2026-09-11).
- `core/gl.hpp`: the one include point for direct GL through raylib's glad (2026-09-11).
- `core/cell.hpp`: `CellType` shared by core and rule (2026-09-11).
- Test support: hidden-window `GlContext` fixture; `[gpu]`-tagged cases skip when no display is available (2026-09-11).
- `ui/`: `App` (window, loop, lifecycle), ImGui panels (rule entry with errors, target rate, pause/step/burst, path toggle, grid resize, fill densities, brush, palette editor, log), canvas (paint/pan/zoom, keyboard shortcuts), `brushSpans`/`strokePoints` geometry (2026-09-11).
- `main.cpp`: real entry point with `--rule`, `--size`, `--cpu`, `--seed`, `--rate`, `--frames`, `--screenshot`, `--gl-check` (2026-09-11).
- `Simulation::paintSpan` and `GpuGrid::uploadRegion`: painting writes host and GPU together with no readback (2026-09-11).
- `render/`: `Renderer2D` (palette pass over raylib's batch, `shaders/palette2d.{vert,frag}` at GLSL 430), `View2D` camera with fit/zoom-at-cursor/pan and pixel-exact snapping, `Palette` with a default hue ramp (2026-09-11).
- `cmake/EmbedShaders.cmake`: the shader-embedding function, shared by sim/ and render/ (2026-09-11).
- `sim/scheduler`: accumulator-driven rate control with pause, single-step, burst, a per-frame step cap and a wall-clock budget; shortfall reported via `Stats::below_target` (2026-09-11).
- `sim/rng`: PCG32 stream A, verified against the reference sequence; `sim/fill`: seeded random fill with per-state densities (2026-09-11).
- `sim/simulation`: `Simulation` owning grid, compiled rule, both steppers, scheduler and stream A; all-or-nothing rule swap; runtime CPU/GPU path switch with state carried across (2026-09-11).
- `shaders/lut_step.comp`: the lookup-table compute step, specialised at compile time per rule shape; embedded into the binary by `cmake/EmbedShader.cmake` (2026-09-11).
- `sim/gpu_step`: `GpuStepper` with a per-shape program cache and SSBO-backed rule data; a rule change of the same shape compiles nothing (2026-09-11).
- CPU/GPU equivalence test: 15 fixtures × 3 boundaries × 1000 generations, 1D/2D/3D, plus a GPU glider test (2026-09-11).
- `rule/lut`: the lookup-table backend — `LutRule` (table, layout, canonical offsets, W table) and `selectBackend` per D-004 (2026-09-11).
- `sim/boundary`: `resolve()` for wrap, zero and mirror, the reference the shader must mirror (2026-09-11).
- `sim/cpu_step`: the CPU reference stepper over a `LutRule`, all table kinds, 1D/2D/3D (2026-09-11).
- `rule/ir`: the RuleIR type per SPEC §4 with Table, Expression and Kernel forms, full validation, a stable 64-bit hash, and enum name conversion (2026-09-11).
- `rule/neighbourhood`: canonical offset enumeration and counts per SPEC §3 (2026-09-11).
- `rule/table_layout`: exact table sizes and index arithmetic per SPEC §5, with count-vector ranking for multi-state outer-totalistic rules (2026-09-11).
- `rule/dsl`: parser for B/S, B/S/C and count-condition table blocks, emitting a Table or an Expression by the §5 threshold (2026-09-11).
- Catch2 test suite (`tests/`) wired into CTest; 162 cases covering the above (2026-09-11).
- CMake build fetching raylib 6.0 (4.3 backend), Dear ImGui 1.92.7 and rlImGui in-tree; `src/main.cpp` opens a window and verifies a compute dispatch, with `--gl-check` for a headless pass/fail (2026-09-11).
- `BUILD.md` with prerequisites, dependency pins and PRIME offload instructions for the NVIDIA GPU (2026-09-11).
- `LICENSE`: Apache-2.0 (2026-09-11).
- Source tree per README §Project structure, empty apart from `.gitkeep` placeholders, and a `.gitignore` (2026-09-11).

### Fixed
- BUG-007: `GpuStepper`'s move constructor dropped later-added fields, so the GPU path of any `Simulation` ran without cell mutation; state is now split into exchanged handles and copied config (2026-09-12).
- BUG-006: append-only lineage versus grid rewind; grid rewind now truncates (2026-09-12).
- BUG-005: SPEC §9.2 took the mutated state from the hash that had just passed the threshold, which would have made every mutation a decay to state 0; the state now comes from a second mixing (2026-09-11).
- BUG-001: SPEC §3 closed form for the 3D von Neumann count (2026-09-11).
- BUG-004: SPEC §5 specified a 1D texture for the table, which cannot hold `LUT_MAX_ENTRIES` on NVIDIA; now an SSBO (2026-09-11).
- BUG-003: SPEC §2 `mirror` did not specify which reflection; resolved as reflection about the edge cell's centre (2026-09-11).
- BUG-002: SPEC §5 multi-state outer-totalistic index encoding contradicted its size formula; resolved as dense lexicographic ranking (2026-09-11).

### Changed
- Palette alpha now means opacity in the 3D view and the default palette makes state 0 transparent; the 2D pass is opaque regardless (2026-09-12).
- F-010's acceptance now names the bundled rule set, with xscreensaver/xlockmore provenance; F-024 Screensaver mode promoted from candidate to Should (Phase 6); stochastic and two-phase rule forms recorded as candidates with the xscreensaver hacks that need them; IMP-002 proposes the `signature_literal` syntax (2026-09-12).
- Per-step compute parameters (generation, mutation threshold, seed B) are uniforms rather than an SSBO update, avoiding a buffer-in-flight write each step (2026-09-11).
- Scripted screenshots are captured before the swap; the back buffer after a swap is undefined on Mesa (2026-09-11).
- The app calls `glFinish()` before scheduling so a driver's deferred vsync throttle is not charged to the step budget (2026-09-11).
- D-012: hexagonal lattices promoted into scope as F-023 (Phase 2); triangular and Penrose recorded as candidates with their costs (2026-09-11).
- SPEC §7 gains notes on `and`/`or` precedence, `n(0)`, comments, and the unspecified `signature_literal` (2026-09-11).
- D-009 project name moved from Proposed to Accepted on author confirmation; GitHub repository created at `Darian-Frey/Aether` (2026-09-11).

### Notes
- Phase 3 complete 2026-09-12: 3D grids rendered as volumes with orbit, clip and slice; 256³ at 49 gen/s and 49 fps on the target GPU.
- Phase 2 complete 2026-09-12: both mutation controls, lineage with pin/rewind, sessions that replay bit-identically across processes, and hexagonal lattices.
- Phase 1 complete 2026-09-11: the 2D discrete core runs interactively with Life, HighLife, Brian's Brain and cyclic CAs at 1024² above 2,000 gen/s on the target GPU, CPU and GPU agreeing bitwise.
- The project name is confirmed (D-009).
