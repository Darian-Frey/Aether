# CLAUDE.md

Current state, not history. Rewrite this file at the end of any significant session.

## Project

Aether is a cellular automata laboratory: one GPU-resident engine running discrete and continuous automata on 1D, 2D and 3D lattices, with rules authored in a declarative DSL or in Lua, and with two independent mutation controls that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session.

## Current state

Phase 1 in progress (started 2026-09-11). The engine runs headless end to end and `render/` can draw it to any framebuffer; there is no UI and `main.cpp` is still the Phase 0 probe.

- Documentation set written 2026-08-30; `BUILD.md` added 2026-09-11.
- `CMakeLists.txt` + `cmake/Dependencies.cmake`: fetch raylib `6.0` (with `OPENGL_VERSION=4.3`), Dear ImGui `v1.92.7`, rlImGui `Raylib_6_0`, Catch2 `v3.9.1`; build them in-tree as static libs.
- `src/core/` (`aether_core`): `cell` (CellType, header-only), `grid` (GridSpec, `PingPong<T>` — the one swap — and HostGrid), `gpu_grid` (GL_R8UI texture pair, upload/download, `queryVram`, VRAM guard), `gl.hpp` (the single glad include). `namespace aether::core`. Boundary handling is deliberately *not* here; it is rule semantics and belongs to the steppers.
- `src/rule/` (`aether_rule`): `ir` (type, validation, hash, names), `neighbourhood` (canonical offsets), `table_layout` (sizes, index arithmetic, `kLutMaxEntries`), `dsl` (B/S, B/S/C, table block → IR), `lut` (`LutRule` + `selectBackend`). `namespace aether::rule`.
- `src/sim/` (`aether_sim`): `boundary` (header-only `resolve()`, the reference for wrap/zero/mirror), `cpu_step` (the oracle; takes distinct current/next spans, or a HostGrid and swaps), `gpu_step` (`GpuStepper`: per-shape program cache keyed on (dims, N, S, kind, boundary); SSBOs for params/offsets/W/table; `step()` dispatches, barriers, swaps), `scheduler` (header-only, pure timing, fake-clock testable), `rng` (PCG32 stream A), `fill` (random seeding), `simulation` (`Simulation`: the object main/ui talk to; owns grid + rule + both paths + scheduler + stream A; `setRule` is all-or-nothing; `setPath` syncs state across; `texture()` for the renderer). `namespace aether::sim`.
- Authority rule in `Simulation`: GPU path → GPU pair is truth, host stale until `syncToHost()`; CPU path → host is truth, mirrored to GPU after each step. Painting goes `host()` → edit → `commitHost()`.
- `src/render/` (`aether_render`): `view2d` (header-only camera: zoom + centre; `snappedOrigin` gives pixel-exact integer zoom; `cellAt` is what the canvas will use so brush and pixels agree by construction), `palette` (256 RGBA + `defaultFor(states)`), `renderer2d` (`Renderer2D::draw(texture, spec, view, viewport, frameW, frameH, states)` inside Begin/EndDrawing; rides raylib's batch via `BeginShaderMode`). `namespace aether::render`.
- `shaders/palette2d.{vert,frag}`: both at `#version 430` (raylib's default VS is 330; do not mix). The FS maps `gl_FragCoord` → cell through `origin`/`zoom` uniforms and discards outside the viewport rect.
- `shaders/lut_step.comp`: the one LUT shader, specialised by `#define`s the stepper prepends. Embedded at build time via `cmake/EmbedShader.cmake` into `generated/shaders/`; `sim/shaders.hpp` declares the symbols. Edit the file in `shaders/`, never the generated copy.
- `tests/`: Catch2, one file per module, run by `ctest`. 105 cases. `tests/sim/equivalence_test.cpp` is the CPU/GPU oracle comparison; run it on the T1200 as well as the iGPU before trusting a shader change. `[gpu]` cases open a hidden window via `tests/support/gl_context.hpp` and SKIP without a display.
- `src/main.cpp`: Phase 0 probe. Opens a window, runs a compute dispatch over an SSBO and verifies it; `--gl-check` does the same headless and exits 0/1. **To be replaced, not extended.**
- `src/ui/`, `rules/`, `patterns/`, `docs/`: empty apart from `.gitkeep`.
- `LICENSE`: Apache-2.0, copyright 2026 Shane Hartley.

Design is settled through D-011. The project name is confirmed (D-009, Accepted 2026-09-11).

Verified on the target machine: GL 4.3 compute works on both the Intel iGPU (Mesa, GL 4.6) and the NVIDIA T1200 (595.84, GL 4.3 context). The D-001 assumption holds.

## Active task

Phase 1 — 2D discrete core. Begin with `rule/ir` and the DSL parser, not with the renderer. The IR is the contract everything else is written against; building the renderer first means writing it twice.

Done: all of `rule/`, `core/`, `sim/` and `render/` for Phase 1, `shaders/`, the equivalence test.

Next, in order: `ui/` ImGui panels (rule entry with error display, rate/pause/step/burst, path toggle, fill densities) and the painting canvas (screen → cell coordinates through the pan/zoom transform; `host()` edit → `commitHost()`; on the GPU path that means a `syncToHost()` first, or a partial upload — decide then) → replace the Phase 0 `main.cpp` with the real loop → Phase 1 acceptance run (Life, HighLife, Brian's Brain, cyclic CA at 1024², ≥ 200 gen/s, CPU/GPU agreeing).

Interim throughput on the T1200: Life 1024² 3,684 gen/s (budget 200); 256³ 3D 69 gen/s (budget 30). The Intel iGPU is at the 2D budget and far below the 3D one.

Open design gaps noticed on the way, logged not fixed: IMP-001 (outer-totalistic tables oversized for single-state-count rules); SPEC §7 `signature_literal` undefined. Spec defects resolved and recorded: BUG-001 to BUG-004.

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
- GL RAII objects (`GpuGrid`, `GpuStepper`, `Renderer2D`) must be destroyed before `CloseWindow()`. Scope them inside the window's lifetime; a destructor after context teardown segfaults.

## Out of scope

Do not change these without asking:

- The IR schema (SPEC §4). Everything else is written against it; changes are breaking and need a DECISIONS entry.
- `LUT_MAX_ENTRIES` and the backend selection rule (D-004, SPEC §5). A tuning constant that silently changes semantics is exactly what AV-007 is about.
- The determinism contract (SPEC §11, D-006). Adding anything wall-clock-dependent or order-dependent to the step loop breaks the session format for every existing file.
- The compile-time-only Lua invariant (D-003). Reversing it needs a superseding decision, not a pragmatic exception.
- Feature scope. Items in FEATURES §Out of scope are settled: no hex lattices, no agent-based automata, no hashlife, no distributed simulation. Candidate features are candidates, not backlog.
- Phase ordering in ROADMAP. Building the renderer or the 3D path before the IR and the DSL means writing them twice.
