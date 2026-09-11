# Architecture

Structural description of Aether. Rationale lives in [DECISIONS.md](DECISIONS.md); this document describes the system as designed, not why.

As of 2026-08-30 no code exists. This describes the intended structure that Phase 1 will build against.

## System overview

```
  ┌──────────────┐        ┌──────────────┐
  │  rule/dsl    │        │  rule/lua    │      front ends
  └──────┬───────┘        └──────┬───────┘
         │                       │
         └──────────┬────────────┘
                    ▼
            ┌──────────────┐
            │  rule/ir     │                   single compile target
            └──────┬───────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
  ┌───────────┐        ┌────────────┐
  │ backend/  │        │ backend/   │           chosen by IR shape
  │ lut       │        │ glsl       │
  └─────┬─────┘        └─────┬──────┘
        └────────┬───────────┘
                 ▼
          ┌─────────────┐        ┌──────────────┐
          │  sim/step   │◀──────▶│  core/grid   │   ping-pong textures
          └──────┬──────┘        └──────┬───────┘
                 │                      │
        ┌────────┴────────┐             ▼
        ▼                 ▼      ┌──────────────┐
  ┌───────────┐   ┌────────────┐ │  render/     │
  │ sim/mutate│   │ sim/lineage│ └──────────────┘
  └───────────┘   └────────────┘
```

The spine is: front end → IR → backend → compute step. Everything else hangs off it. `sim/mutate` writes back into the IR (rule mutation) and into the compute step (cell mutation) — the two arrows that make this a laboratory rather than a viewer.

## Module responsibilities

### `core/`
Owns the grid and the session. The grid is a dense uniform lattice held as a pair of GPU textures swapped each generation, with a matching host-side array used only by the CPU reference path and by save/load. Also owns serialisation: writing and reading the session file described in SPEC §11, including the format version check. `core/` knows nothing about rules; it holds state and hands it to whoever steps it.

### `rule/`
Everything between a rule as text and a rule as executable code. Contains the DSL parser, the sandboxed Lua host, the IR type itself, IR validation, and the two compiler backends. The IR is the module's public contract — nothing outside `rule/` constructs an IR by hand, and nothing outside `rule/` sees a lookup table or a shader string. Backend selection happens here, from IR shape alone, and is reported outward as metadata rather than exposed as a choice.

### `sim/`
Drives time. The scheduler holds the accumulator that converts a target generations-per-second into a variable number of steps per frame, and owns pause, single-step and burst. The mutation engine owns both RNG streams and both mutation mechanisms: rule mutation runs here on the CPU between generations and triggers a recompile through `rule/`; cell mutation is a parameter passed into the compute step and evaluated on the GPU. The lineage log records every rule the run has passed through, indexed by the generation at which it took effect, and supports pinning and rewinding.

### `render/`
Presentation only; never mutates grid state. Two paths sharing a palette: a 2D path sampling the state texture through a palette lookup with pan and zoom, and a 3D path raymarching the volume texture with per-state colour and opacity. The 1D space-time diagram is a variant of the 2D path with a scrolling write cursor rather than a full-grid read.

### `ui/`
Dear ImGui panels for rule entry, simulation parameters, mutation controls, palette editing and the lineage browser, plus the drawing canvas that writes cells into `core/`. The canvas is the one place where user input mutates grid state directly, and it does so through an explicit `core/` API rather than by touching textures.

### `shaders/`
The compute shader for the lookup-table path, the codegen template for the generated-GLSL path, and the fragment shaders for both render paths. Not a code module, but a versioned artifact directory: shader source changes are as breaking as C++ changes and are treated the same way in commits.

## Key invariants

1. **The IR is the only compile target.** A front end that reaches past the IR to a backend, or a backend that inspects DSL text, is a defect regardless of whether it works.
2. **Lua runs at compile time only.** The Lua interpreter is not linked into any per-cell path and must not be reachable from the step loop. This is architectural, not stylistic — see AV-008.
3. **The step never reads the buffer it writes.** Ping-pong swap happens between generations, in exactly one place.
4. **CPU and GPU paths implement the same rule semantics.** Including boundary handling and cell mutation. Divergence is a bug in whichever path disagrees with SPEC, not a tolerable difference.
5. **Randomness comes from named, seeded streams.** No unseeded RNG, no `rand()`, no time-derived seeds anywhere in `sim/` or in shaders. Cell mutation is hashed from coordinate and generation so it is independent of evaluation order.
6. **Rendering is read-only with respect to simulation state.**
7. **Every rule the simulation has ever run is in the lineage log.** Rule mutation without a lineage append is an incomplete operation.

## Cross-cutting concerns

**Threading.** Single-threaded on the host. The GPU does the parallel work; the CPU reference path is deliberately serial for clarity and determinism, since it exists to be trusted rather than to be fast. Shader compilation for the codegen backend happens on the main thread and blocks — acceptable because it happens at rule-change time, not per frame.

**Error handling.** Rule compilation failures are recoverable and reported to the UI with position information; the previously compiled rule stays active and the simulation continues. Failures that invalidate engine state — a grid allocation that does not fit in VRAM, an unsupported GL version at startup — abort with a diagnostic rather than degrading. There is no software fallback path.

**Determinism.** Treated as a cross-cutting property rather than a feature. The session quadruple (initial state, rule, seeds, mutation schedule) is sufficient to replay any run; anything that would break this — a wall-clock-derived value entering the step, a floating-point accumulation whose order varies with frame rate — is an architectural violation.

**Logging.** Rule compilation events, mutation events and backend selection are logged at info level to a ring buffer visible in the UI. The step loop logs nothing.

**Memory.** GPU allocation is bounded up front: grid dimensions and state count determine the texture footprint, checked against available VRAM before allocation. Host-side allocation in the step loop is prohibited on both paths.
