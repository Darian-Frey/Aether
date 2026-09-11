# CLAUDE.md

Current state, not history. Rewrite this file at the end of any significant session.

## Project

Aether is a cellular automata laboratory: one GPU-resident engine running discrete and continuous automata on 1D, 2D and 3D lattices, with rules authored in a declarative DSL or in Lua, and with two independent mutation controls that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session.

## Current state

Phase 0 complete (2026-09-11). Builds; no engine code.

- Documentation set written 2026-08-30; `BUILD.md` added 2026-09-11.
- `CMakeLists.txt` + `cmake/Dependencies.cmake`: fetch raylib `6.0` (with `OPENGL_VERSION=4.3`), Dear ImGui `v1.92.7`, rlImGui `Raylib_6_0`; build them in-tree as static libs.
- `src/main.cpp`: Phase 0 probe. Opens a window, runs a compute dispatch over an SSBO and verifies it; `--gl-check` does the same headless and exits 0/1. **To be replaced by Phase 1, not extended.**
- `src/{core,rule,sim,render,ui}/`, `shaders/`, `rules/`, `patterns/`, `tests/`, `docs/`: empty apart from `.gitkeep`.
- `LICENSE`: Apache-2.0, copyright 2026 Shane Hartley.

Design is settled through D-011. The project name is confirmed (D-009, Accepted 2026-09-11).

Verified on the target machine: GL 4.3 compute works on both the Intel iGPU (Mesa, GL 4.6) and the NVIDIA T1200 (595.84, GL 4.3 context). The D-001 assumption holds.

## Active task

Phase 1 — 2D discrete core. Begin with `rule/ir` and the DSL parser, not with the renderer. The IR is the contract everything else is written against; building the renderer first means writing it twice.

Suggested order within Phase 1: `rule/ir` (type + validation + hash) → DSL parser for B/S and B/S/C → LUT backend (table size computed *before* allocation, AV-010) → `core/` grid + ping-pong pair → CPU reference stepper → compute shader for the LUT path → equivalence test → scheduler → canvas, random fill, palette rendering.

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
ctest --test-dir build           # no tests yet
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
- rlgl does not wrap `glMemoryBarrier`. Direct GL goes through `external/glad.h` from the fetched raylib tree (function pointers live in `libraylib`). Keep that include confined to the one place that owns the ping-pong step.
- raylib 6.0 renamed `rlCompileShader` → `rlLoadShader` and `rlLoadComputeShaderProgram` → `rlLoadShaderProgramCompute`. Older examples online use the old names.

## Out of scope

Do not change these without asking:

- The IR schema (SPEC §4). Everything else is written against it; changes are breaking and need a DECISIONS entry.
- `LUT_MAX_ENTRIES` and the backend selection rule (D-004, SPEC §5). A tuning constant that silently changes semantics is exactly what AV-007 is about.
- The determinism contract (SPEC §11, D-006). Adding anything wall-clock-dependent or order-dependent to the step loop breaks the session format for every existing file.
- The compile-time-only Lua invariant (D-003). Reversing it needs a superseding decision, not a pragmatic exception.
- Feature scope. Items in FEATURES §Out of scope are settled: no hex lattices, no agent-based automata, no hashlife, no distributed simulation. Candidate features are candidates, not backlog.
- Phase ordering in ROADMAP. Building the renderer or the 3D path before the IR and the DSL means writing them twice.
