# CLAUDE.md

Current state, not history. Rewrite this file at the end of any significant session.

## Project

Aether is a cellular automata laboratory: one GPU-resident engine running discrete and continuous automata on 1D, 2D and 3D lattices, with rules authored in a declarative DSL or in Lua, and with two independent mutation controls that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session.

## Current state

Phases 1–4 complete (2026-09-11 to 2026-09-15). The discrete core runs interactively in 2D (square and hexagonal) and 3D (volume raymarch, orbit, clip, slice painting) with both mutation controls, a lineage log with pin and rewind, and sessions that replay bit-identically across processes. Cells can be given an ageing tail and mutation can be grouped into blocks. Rules come from the DSL or from Lua, run through either the table or the codegen backend, and fourteen of them ship in `rules/`. Phase 5 (continuous states) is not started.

- Documentation set written 2026-08-30; `BUILD.md` added 2026-09-11.
- `CMakeLists.txt` + `cmake/`: `Dependencies.cmake` fetches raylib `6.0` (with `OPENGL_VERSION=4.3`), Dear ImGui `v1.92.7`, rlImGui `Raylib_6_0`, nlohmann/json `v3.12.0`, Catch2 `v3.9.1`, and finds Lua 5.4 through pkg-config rather than fetching it; `EmbedShaders.cmake` turns `shaders/*` into string constants under `build/generated/`.
- `src/core/` (`aether_core`): `cell`, `grid` (GridSpec, `PingPong<T>` — the one swap — HostGrid), `gpu_grid` (GL_R8UI texture pair, upload/download/uploadRegion, `queryVram`, VRAM guard), `gl.hpp` (the single glad include). Boundary handling is deliberately *not* here.
- `src/rule/` (`aether_rule`): `ir` (type, validation, hash, names; `metadata.decay_from` is a presentation hint, not semantics), `decay` (`applyDecay`/`maxDecay`: the `decay N` desugaring, IR in and IR out), `ir_json` (IR ↔ JSON, base64; the one place an IR is built from external data, validated), `neighbourhood`, `table_layout` (sizes and index arithmetic for all four table kinds, `kLutMaxEntries`), `dsl` (B/S, B/S/C, table block with count conditions, signature literals and `decay` → IR), `compile` (`CompiledRule` + `compileRule` + `selectBackend`: a table with its layout and auxiliary data, or an expression with the GLSL for it), `glsl` (`generateGlsl`: expression IR → the `aether_rule` function of SPEC §6), `library` (`LibraryRule`, `parseRuleFile`, `loadLibrary`, `saveRule`: a rule file is its source with a comment header, inert in both languages, so nothing is stripped), `lua` (`compileLua`: a script per compile in a RAII `lua_State` with its own `_ENV`, instruction and memory budgets).
- `src/sim/` (`aether_sim`): `boundary` (`resolve()`, the reference for wrap/zero/mirror), `cpu_step` (the oracle: table kinds, and the expression interpreter that mirrors `rule/glsl`), `gpu_step` (`GpuStepper`; the program cache is keyed on shape, plus `ir_hash` for generated rules), `scheduler` (pure timing), `rng` (PCG32 stream A), `hash` (stream B: `hash32`, `blockHash`, `mutationThreshold`, `CellMutation` with its block shift, `mutatedState` — twin of `shaders/hash.glsl`), `fill` (`fillRandom`, and `defaultDensity` — the one place a fresh grid's seeding weights are decided), `rule_mutation` (`mutateRule`: point edits from stream A, validate-or-redraw ×8; `RuleMutationParams`), `lineage` (`Lineage`: entries with full IR, origin, journal index, pin), `journal` (event types), `session` (`Session` struct, cell codec, JSON, save/load with sidecar), `simulation` (`Simulation`: grid + rule + both paths + scheduler + stream A + lineage; `setRule` is all-or-nothing and *every* successful install appends to the lineage — `installRule` is the single route, so mutation cannot bypass the log; `maybeMutateRule` runs at the top of `step()`; `rewind(i)` reinstalls entry i and records itself; every user-reachable mutator journals itself with the generation; `session()` snapshots; `resume()` continues from stored state; `replay()` rebuilds from initial + journal; `rewindGrid()` is replay-then-truncate; `setPath` syncs; `paintSpan` writes host and GPU with no readback; `texture()` for the renderer).
- `src/render/` (`aether_render`): `view2d` (camera with `Lattice::{Square,Hex}`; pixel-exact snapping on square; axial↔cell-space matrices and `hexRound` on hex; `cellAt` shared with the canvas), `palette` (alpha = 3D opacity), `renderer2d` (palette pass over raylib's batch; `lattice` uniform), `orbit` (header-only orbit camera: `rayFor`, `pickOnSlab`, `fit`), `renderer3d` (`shaders/volume.frag`: Amanatides–Woo DDA, bounded by W+H+D; binds its textures on units 6/7 by hand because raylib's sampler registration is 2D-only).
- `src/ui/` (`aether_ui`): `app` (window, loop, lifecycle, `Options`, session save/load/verify, `adoptSimulation` after load/rewind, `is3D()` selects renderer and input), `panels` (ImGui: the transport bar across the top, the left column of collapsing sections ordered by task, the viewport overlay, and a Keys list; `kLabelColumn` is what keeps widget labels from clipping), `canvas` (2D: paint/pan/zoom; 3D: orbit/zoom, slice painting via `paintAt3D`; keys), `brush` (pure geometry), `log` (ring buffer), `headless` (`headless`/`replay`/`compare` subcommands; exit 77 = no GL context, which CTest treats as skip).
- `src/main.cpp`: subcommand dispatch (`headless`, `replay`, `compare`) and argument parsing → `ui::App::run()`. `--gl-check` is the compute-path probe; `--frames N --screenshot F` gives a scripted run; `--load FILE` resumes a session.
- `shaders/`: `hash.glsl` (prepended to every shader that mutates cells), `lut_step.comp` (specialised by `#define`s the stepper prepends), `palette2d.{vert,frag}` (both GLSL 430). Edit these, never the generated copies.
- `tests/`: Catch2, one file per module, 217 cases plus the five cross-process `replay.*` cases under `ctest` (222). `tests/support/table.hpp` reads a table entry without caring which indexing scheme the kind uses — use it rather than calling an index function directly. One of them compiles every bundled rule, so a broken rule file fails the suite. `[gpu]` cases open a hidden window and SKIP without a display. `tests/sim/equivalence_test.cpp` is the CPU/GPU oracle comparison; run it on the T1200 as well as the iGPU before trusting a shader change.
- The window is three regions, laid out by `App::layOut()` every frame: a transport strip across the top, a fixed left panel, and the viewport. `viewport_` and `panelRect_` are what everything else measures against.
- `rules/`: fourteen bundled rules (`.rule` = DSL, `.lua` = Lua). Searched at run time as `$AETHER_RULES`, `./rules`, `<exe>/rules`, `<exe>/../rules`; the first directory with rules wins.
- `patterns/`, `docs/`: empty apart from `.gitkeep`.

Authority rule in `Simulation`: GPU path → GPU pair is truth, host stale until `syncToHost()`; CPU path → host is truth, mirrored to GPU after each step. Painting goes through `paintSpan`, which writes both.

Throughput on the T1200: Life 1024² 3,684 gen/s (budget 200); a generated 16-state rule at 1024² about 2,900 gen/s; 256³ 3D 69 gen/s stepping alone, 49 gen/s and 49 fps with the volume rendered (budget 30/30). Generating and compiling a rule costs 61 ms on a session's first compile and under 2 ms after (budget 250). The Intel iGPU is at the 2D budget and far below the 3D one; every SPEC §12 figure is a T1200 figure.

## Active task

Phase 5 — continuous states (F-006, D-010). The IR has carried `cell_type` and the `Kernel` form since day one for exactly this; both backends currently refuse `f32` with a diagnostic, which is the stub D-010 asked for. Suggested order:

1. `core/`: the `f32` grid path. `GridSpec` already carries `CellType::F32` and `GpuGrid` already selects `GL_R32F`; what is missing is `HostGrid` holding floats (it is a byte vector today) and the cell codec in `sim/session`. Decide early whether `HostGrid` keeps a byte buffer with typed views or becomes a variant — the byte buffer with views is probably less churn.
2. `rule/`: kernel authoring. A radial profile sampled to a matrix, or an explicit matrix; the growth function is an `Expression` over the convolution result. Lua can express one today with almost no new code, so Lua first is the cheaper route to a running SmoothLife; the DSL syntax can follow.
3. `sim/`: the continuous step. Convolution then growth, on both paths. The kernel's support is the neighbourhood, so the offsets already describe it.
4. `rule/glsl`: `aether_rule_f(float self, float conv)` per SPEC §6, and the float half of the expression interpreter — it exists but is untested, because nothing produces float expressions yet.
5. Rendering: `f32` cells through the palette. The 2D shader samples `usampler2D` and will need a float variant.
6. AV-015: cross-machine float determinism. Expect to narrow the determinism claim for `f32` rather than to guarantee it — SPEC §11 anticipates that, and the honest outcome may be that `f32` sessions replay on one machine only.

Open: nothing in BUGS or IMPROVEMENTS. F-024 screensaver mode is a Should in Phase 6, along with the 1D space-time view the elementary rules are waiting for. Langton's loops is still untranscribed. IMP-001, IMP-002 and IMP-003 are applied.

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
- `TakeScreenshot` takes a path relative to raylib's base directory and quietly mangles an absolute one, so a scripted run must `cd` to where the image should land. It must also run before `EndDrawing`: after the swap the back buffer is undefined (black on Mesa).
- Objects that own GL handles and are moved (`GpuStepper`, `GpuGrid`, `Renderer2D`) keep plain state in a struct copied wholesale and handles in a struct exchanged on move. Do not add a member outside those structs: a hand-listed move constructor silently drops it (BUG-007).
- Every user-reachable `Simulation` mutator must journal itself. A new one that does not breaks replay silently; the `replay.*` CTest and `tests/sim/session_test.cpp` are the guard.
- The volume shader binds its samplers on texture units 6 and 7 itself; `rlSetUniformSampler` binds `GL_TEXTURE_2D` and cannot take a 3D texture. Units 0–4 are raylib's.
- Hex lattices are axial storage: a W×H grid is a rhombus on screen, `wrap` is a rhombic torus, and hex neighbourhoods are 2D only. `View2D::hexRound` and the shader's `hexRound` are twins.
- Lua is a compile-time dependency of `rule/` only, linked PRIVATE, and taken from the system rather than fetched. Nothing outside `rule/lua.cpp` includes a Lua header, and `compileLua` returns a plain IR: that is what makes AV-008 structural.
- `rot` in a signature literal derives its permutation from the neighbourhood's offsets (`rotationPermutation`), so it holds at any radius on square and hex lattices and is refused in 1D and 3D rather than guessed at.
- `rule/glsl.cpp` and the expression interpreter in `cpu_step.cpp` are twins, like the hash pair. Division by zero yields zero, integer arithmetic wraps at 32 bits, and the result is clamped to the state range — in both, or the backends diverge. The backend equivalence test is the guard.
- A rule's kind decides its index arithmetic, and there are now four table kinds. When adding one, the places that must all agree are `tableSize`, `TableLayout`, `cpu_step`, the `AETHER_KIND` branch in `lut_step.comp`, `compileLut`'s auxiliary buffer, `ir_json`, and the validator. The equivalence suite is what proves they do.
- `counted_totalistic` is the form almost every real rule wants (D-016). The DSL picks it when a rule asks about at most one state per own state; it is used only when strictly smaller, which is why binary rules keep the hashes they had.
- A rule's ageing tail is ordinary states, not a new concept: `decay` desugars in `rule/decay` and everything downstream sees a normal `outer_totalistic` IR. `metadata.decay_from` is a hint for palettes only — it is outside `ir_hash`, so a wrong value gives odd colours, never a different automaton.
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
