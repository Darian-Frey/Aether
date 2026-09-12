# CLAUDE.md

Current state, not history. Rewrite this file at the end of any significant session.

## Project

Aether is a cellular automata laboratory: one GPU-resident engine running discrete and continuous automata on 1D, 2D and 3D lattices, with rules authored in a declarative DSL or in Lua, and with two independent mutation controls that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session.

## Current state

Phases 1 and 2 complete (2026-09-11, 2026-09-12). The 2D core runs interactively on square and hexagonal lattices with both mutation controls, a lineage log with pin and rewind, and sessions that replay bit-identically across processes. Phase 3 (three dimensions) not started.

- Documentation set written 2026-08-30; `BUILD.md` added 2026-09-11.
- `CMakeLists.txt` + `cmake/`: `Dependencies.cmake` fetches raylib `6.0` (with `OPENGL_VERSION=4.3`), Dear ImGui `v1.92.7`, rlImGui `Raylib_6_0`, nlohmann/json `v3.12.0`, Catch2 `v3.9.1`; `EmbedShaders.cmake` turns `shaders/*` into string constants under `build/generated/`.
- `src/core/` (`aether_core`): `cell`, `grid` (GridSpec, `PingPong<T>` — the one swap — HostGrid), `gpu_grid` (GL_R8UI texture pair, upload/download/uploadRegion, `queryVram`, VRAM guard), `gl.hpp` (the single glad include). Boundary handling is deliberately *not* here.
- `src/rule/` (`aether_rule`): `ir` (type, validation, hash, names), `ir_json` (IR ↔ JSON, base64; the one place an IR is built from external data, validated), `neighbourhood`, `table_layout` (sizes, index arithmetic, `kLutMaxEntries`), `dsl` (B/S, B/S/C, table block → IR), `lut` (`LutRule` + `selectBackend`).
- `src/sim/` (`aether_sim`): `boundary` (`resolve()`, the reference for wrap/zero/mirror), `cpu_step` (the oracle), `gpu_step` (`GpuStepper`, per-shape program cache, SSBOs), `scheduler` (pure timing), `rng` (PCG32 stream A), `hash` (stream B: `hash32`, `mutationThreshold`, `CellMutation`, `mutatedState` — twin of `shaders/hash.glsl`), `fill`, `rule_mutation` (`mutateRule`: point edits from stream A, validate-or-redraw ×8; `RuleMutationParams`), `lineage` (`Lineage`: entries with full IR, origin, journal index, pin), `journal` (event types), `session` (`Session` struct, cell codec, JSON, save/load with sidecar), `simulation` (`Simulation`: grid + rule + both paths + scheduler + stream A + lineage; `setRule` is all-or-nothing and *every* successful install appends to the lineage — `installRule` is the single route, so mutation cannot bypass the log; `maybeMutateRule` runs at the top of `step()`; `rewind(i)` reinstalls entry i and records itself; every user-reachable mutator journals itself with the generation; `session()` snapshots; `resume()` continues from stored state; `replay()` rebuilds from initial + journal; `rewindGrid()` is replay-then-truncate; `setPath` syncs; `paintSpan` writes host and GPU with no readback; `texture()` for the renderer).
- `src/render/` (`aether_render`): `view2d` (camera with `Lattice::{Square,Hex}`; pixel-exact snapping on square; axial↔cell-space matrices and `hexRound` on hex; `cellAt` shared with the canvas), `palette`, `renderer2d` (palette pass over raylib's batch; `lattice` uniform).
- `src/ui/` (`aether_ui`): `app` (window, loop, lifecycle, `Options`, session save/load/verify, `adoptSimulation` after load/rewind), `panels` (ImGui), `canvas` (paint/pan/zoom/keys), `brush` (pure geometry), `log` (ring buffer), `headless` (`headless`/`replay`/`compare` subcommands; exit 77 = no GL context, which CTest treats as skip).
- `src/main.cpp`: subcommand dispatch (`headless`, `replay`, `compare`) and argument parsing → `ui::App::run()`. `--gl-check` is the compute-path probe; `--frames N --screenshot F` gives a scripted run; `--load FILE` resumes a session.
- `shaders/`: `hash.glsl` (prepended to every shader that mutates cells), `lut_step.comp` (specialised by `#define`s the stepper prepends), `palette2d.{vert,frag}` (both GLSL 430). Edit these, never the generated copies.
- `tests/`: Catch2, one file per module, 150 cases plus the five cross-process `replay.*` cases under `ctest` (155). `[gpu]` cases open a hidden window and SKIP without a display. `tests/sim/equivalence_test.cpp` is the CPU/GPU oracle comparison; run it on the T1200 as well as the iGPU before trusting a shader change.
- `rules/`, `patterns/`, `docs/`: empty apart from `.gitkeep`.

Authority rule in `Simulation`: GPU path → GPU pair is truth, host stale until `syncToHost()`; CPU path → host is truth, mirrored to GPU after each step. Painting goes through `paintSpan`, which writes both.

Throughput on the T1200: Life 1024² 3,684 gen/s (budget 200); 256³ 3D 69 gen/s (budget 30). The Intel iGPU is at the 2D budget and far below the 3D one; every SPEC §12 figure is a T1200 figure.

## Active task

Phase 3 — three dimensions (F-004, F-019). The engine already steps 3D grids (`GpuGrid` 3D textures, `lut_step.comp` 3D variant, 3D equivalence fixtures, 69 gen/s at 256³ on the T1200); what is missing is everything that lets a person see and touch one. Suggested order:

1. `render/renderer3d`: front-to-back raymarch through the `GL_TEXTURE_3D` state texture with per-state colour and opacity from the palette (SPEC §13), step count from grid extent, adjustable clipping planes, single-slice mode. A new fragment shader riding raylib's batch like `Renderer2D`; both stages GLSL 430. Orbit camera as a pure struct like `View2D`.
2. `ui/`: grid panel gains depth and a 2D/3D switch (the `Simulation` is created with `dimensions = 3`); a 3D view section (orbit, clip, slice); painting on a selectable axis-aligned slice through `paintSpan` with `z` (F-011).
3. VRAM guard in the UI: refuse a grid the `checkFootprint` rejects with the message it gives; it already exists in `GpuGrid::create`.
4. `--size WxHxD` on the command line; sessions already carry `d`.
5. Acceptance: a 3D life variant at 256³ at ≥ 30 gen/s and ≥ 30 fps within 4 GB, same IR as its 2D counterpart where the family permits.

Open design gaps, logged not fixed: IMP-001 (outer-totalistic tables oversized for single-state-count rules); SPEC §7 `signature_literal` undefined. Spec defects resolved and recorded: BUG-001 to BUG-006; BUG-007 was a code defect (moved stepper).

## Architectural invariants

These are from ARCHITECTURE.md §Key invariants. Violating one is a defect even if the result works:

1. The IR is the only compile target. Front ends never reach past it to a backend; backends never inspect DSL text.
2. Lua runs at compile time only and is not reachable from the step loop (D-003, AV-008).
3. The step never reads the buffer it writes. The ping-pong swap lives in exactly one place (AV-004).
4. CPU and GPU paths implement identical semantics, boundary handling and cell mutation included (AV-005, AV-007).
5. All randomness comes from named seeded streams A and B (SPEC §10). No `rand()`, no `std::random_device`, no time-derived or thread-index-derived values in `sim/` or `shaders/`.
6. Rendering never mutates simulation state.
7. Rule mutation without a lineage-log append is an incomplete operation.
8. No host allocation inside the step loop on either path.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aether --gl-check        # 0 = compute path works
ctest --test-dir build           # or ./build/tests/aether_tests for Catch2 output
```

First configure fetches three dependencies; see `BUILD.md`. To run on the T1200 rather than the Intel iGPU, prefix with `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`. SPEC §12 figures are meaningless without it.

The test that matters most from Phase 1 onward is CPU/GPU equivalence. If it is red, nothing else is trustworthy.

## Conventions

- British English throughout, in documentation, comments and commit messages.
- ISO 8601 dates everywhere.
- Stable append-only ID registers: `F-NNN` features, `D-NNN` decisions, `AV-NNN` attack vectors, `BUG-NNN`, `IMP-NNN`. Withdrawn or superseded entries keep their IDs and gain a status flag; they are never deleted or renumbered.
- Refresh the README's `Last reviewed` date whenever the project is opened after more than two weeks away.
- Docs are part of the commit. A code change that invalidates a document without updating it is an incomplete commit.
- Log when found, not silently acted on: a bug discovered or an improvement noticed while working on something else goes into `BUGS.md` / `IMPROVEMENTS.md` before it is fixed or applied. The author decides whether to act. This one is specifically directed at AI partners, which default to fixing things they notice.
- Commit messages are multi-paragraph and carry exactly one Subtle Chaos anomaly each — one small unexplained irregularity in otherwise ordinary professional prose, never placed at the centre of the sentence and never explained. Development-flavoured anomalies preferred. See the Subtle Chaos spec for the archetypes and metrics.

## Known pitfalls

`ATTACK_VECTORS.md` is the canonical list. The three most likely to be hit early:

- **AV-004 (buffer aliasing).** Produces output that looks like a cellular automaton but is not the one specified. The glider-displacement test catches it; run it before trusting anything visual.
- **AV-010 (table size computed after allocation).** Compute the table size from the IR before allocating anything. A non-totalistic 3D Moore rule needs 1.3×10⁸ entries and will exhaust memory during what looks like a routine rule change.
- **AV-006 (ambient randomness).** One stray `rand()` invalidates the entire session format. Check SPEC §10 before adding any stochastic behaviour.

Build-specific:

- raylib must be built with `OPENGL_VERSION=4.3`; under the default 3.3 backend the compute entry points are silent no-ops. `cmake/Dependencies.cmake` forces this. `rlGetVersion() == RL_OPENGL_43` is the runtime assertion.
- rlgl does not wrap `glMemoryBarrier`, integer texture formats or memory-info queries. Direct GL goes through `core/gl.hpp` (raylib's glad; function pointers live in `libraylib`). Include that header, never glad directly, so direct GL use is greppable. `aether_core` exports the include path.
- raylib 6.0 renamed `rlCompileShader` → `rlLoadShader` and `rlLoadComputeShaderProgram` → `rlLoadShaderProgramCompute`. Older examples online use the old names.
- GLSL `%` on a negative operand is undefined. `lut_step.comp` normalises coordinates with non-negative arithmetic before any `%`; keep it that way when touching boundary code.
- `GL_MAX_TEXTURE_SIZE` bounds 1D textures too (32768 on NVIDIA). Tables live in SSBOs for that reason (BUG-004).
- raylib batch + custom samplers: `rlSetShader` (inside `BeginShaderMode`) flushes the batch, and every flush clears `activeTextureId[]`. Call `BeginShaderMode` *before* `rlSetUniformSampler`, or the textures registered are gone by the time the quad draws. `Renderer2D::draw` is the worked example.
- Per-step values reach the compute shader as uniforms. Do not move them back into an SSBO: a `glBufferSubData` on a buffer the previous frame still references stalls on some drivers.
- The scheduler's wall-clock budget cannot see GPU time; the frame-time feedback (`setSlowFrame`) is what keeps the UI alive under an unreachable target. `App` calls `glFinish()` before `frame(dt)` so Mesa's deferred vsync throttle is not charged to the first step.
- `TakeScreenshot` must run before `EndDrawing`: after the swap the back buffer is undefined (black on Mesa).
- Objects that own GL handles and are moved (`GpuStepper`, `GpuGrid`, `Renderer2D`) keep plain state in a struct copied wholesale and handles in a struct exchanged on move. Do not add a member outside those structs: a hand-listed move constructor silently drops it (BUG-007).
- Every user-reachable `Simulation` mutator must journal itself. A new one that does not breaks replay silently; the `replay.*` CTest and `tests/sim/session_test.cpp` are the guard.
- Hex lattices are axial storage: a W×H grid is a rhombus on screen, `wrap` is a rhombic torus, and hex neighbourhoods are 2D only. `View2D::hexRound` and the shader's `hexRound` are twins.
- `sim/hash.hpp` and `shaders/hash.glsl` are twins. Change both or neither; `tests/sim/hash_test.cpp` compares them on the GPU.
- GL RAII objects (`GpuGrid`, `GpuStepper`, `Renderer2D`) must be destroyed before `CloseWindow()`. Scope them inside the window's lifetime; a destructor after context teardown segfaults.

## Out of scope

Do not change these without asking:

- The IR schema (SPEC §4). Everything else is written against it; changes are breaking and need a DECISIONS entry.
- `LUT_MAX_ENTRIES` and the backend selection rule (D-004, SPEC §5). A tuning constant that silently changes semantics is exactly what AV-007 is about.
- The determinism contract (SPEC §11, D-006). Adding anything wall-clock-dependent or order-dependent to the step loop breaks the session format for every existing file.
- The compile-time-only Lua invariant (D-003). Reversing it needs a superseding decision, not a pragmatic exception.
- Feature scope. Items in FEATURES §Out of scope are settled: no agent-based automata, no hashlife, no distributed simulation, no lattices without integer coordinates. Hexagonal lattices are in (D-012, F-023, delivered); triangular and Penrose are candidates. Candidate features are candidates, not backlog.
- Phase ordering in ROADMAP. Building the renderer or the 3D path before the IR and the DSL means writing them twice.
