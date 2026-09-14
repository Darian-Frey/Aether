> **Status:** Active
> **Provenance:** Shane Hartley (author); Claude (documentation scaffold, 2026-08-30)
> **Last reviewed:** 2026-08-30
> **Why this status:** Phases 1–3 complete (2D and 3D discrete core, hex lattices, mutation, lineage, sessions); Phase 4 (Lua and codegen) not yet started.

# Aether

Aether is a cellular automata laboratory for Linux. It runs discrete and continuous automata on 1D, 2D and 3D square lattices and 2D hexagonal ones from a single GPU-resident engine, with rules authored either in a compact declarative notation or in Lua, and with two independent stochastic controls — rule mutation and cell mutation — that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session: initial state, rule, seeds, mutation schedule.

The name was confirmed on 2026-09-11 (see D-009).

## Quick start

```bash
git clone https://github.com/Darian-Frey/Aether.git
cd Aether
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aether --gl-check                   # exit 0 means compute shaders work here
./build/aether --rule B3/S23 --size 512x512
./build/aether --rule B5/S45 --size 128x128x128   # a depth makes it 3D
```

In the window: left-drag paints, right-drag pans, wheel zooms; Space pauses, N steps, R refills, C clears, F fits, `[`/`]` change the brush radius, 0–9 pick the brush state. In 3D, right-drag orbits, the View section clips the volume or shows a single slice (S), and painting happens on that slice; each state's palette alpha is its opacity. Rules go in the text box — `B3/S23`, `B2/S/C3`, or a table block such as `states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 3 or n(1) > 4 -> 0;` — and compile with Ctrl+Enter. A table block can also match the neighbourhood exactly — `0: [1, _, 2, _] rot -> 3;` — which is how rotation-symmetric automata are usually published. Add `decay N;` to a table-block rule to give its cells a life cycle: a cell the rule stops supporting fades through N states instead of vanishing, and is coloured as it ages. The Mutation section turns on cell mutation (a probability per cell, optionally grouped into blocks so noise arrives in clumps) and rule mutation (point edits to the rule every N generations); every rule the run passes through is in the Lineage list, where it can be pinned by name or rewound to. `--rule-mutation 250:1 --cell-mutation 0.0001` starts with both on. The Session section saves and loads `.aether` files; a saved run replays bit-for-bit from its initial state — Verify replay checks it on the other execution path, and `aether replay in.aether out.aether` does it headlessly. See [BUILD.md](BUILD.md) for prerequisites and for running on the NVIDIA GPU on an Optimus laptop.

## Build requirements

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | C++20 | GCC 12+ or Clang 15+ |
| CMake | 3.20+ | |
| raylib | 6.0 | Fetched and built in-tree with the 4.3 rlgl backend |
| rlImGui + Dear ImGui | `Raylib_6_0` / 1.92.7 | Fetched and built in-tree |
| Lua | 5.4 | Rule scripting front end (Phase 4) |
| OpenGL | 4.3 core | Compute shaders are mandatory, not optional |

The OpenGL 4.3 requirement is load-bearing. Aether has no fallback renderer; see D-001. Full prerequisites and pins are in [BUILD.md](BUILD.md).

## Project structure

```
aether/
├── cmake/           Dependency fetch and build
├── src/
│   ├── core/        Grid, session, serialisation
│   ├── rule/        DSL parser, Lua front end, IR, compiler backends
│   ├── sim/         Scheduler, mutation engine, lineage log
│   ├── render/      2D and 3D presentation
│   ├── ui/          Control panel, drawing canvas
│   └── main.cpp
├── shaders/         Compute and fragment shaders, plus codegen templates
├── rules/           Bundled rule library
├── patterns/        RLE pattern library
├── tests/
└── docs/
```

## Documentation

- [Features](FEATURES.md) — capabilities, priorities, acceptance criteria
- [Roadmap](ROADMAP.md) — phased plan
- [Architecture](ARCHITECTURE.md) — module boundaries and data flow
- [Decisions](DECISIONS.md) — design rationale and reversal conditions
- [Spec](SPEC.md) — rule IR, DSL grammar, mutation semantics, session format
- [Attack vectors](ATTACK_VECTORS.md) — failure modes and detection
- [Bugs](BUGS.md) — realised defects
- [Improvements](IMPROVEMENTS.md) — candidate refactors
- [Changelog](CHANGELOG.md) — version history
- [Claude handoff](CLAUDE.md) — AI session entry point
- [Build](BUILD.md) — prerequisites, dependency pins, GPU selection

## Licence

Apache-2.0. See [LICENSE](LICENSE).
