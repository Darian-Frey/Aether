> **Status:** Active
> **Provenance:** Shane Hartley (author); Claude (documentation scaffold, 2026-08-30)
> **Last reviewed:** 2026-08-30
> **Why this status:** Documentation scaffold complete; Phase 1 implementation not yet started.

# Aether

Aether is a cellular automata laboratory for Linux. It runs discrete and continuous automata on 1D, 2D and 3D lattices from a single GPU-resident engine, with rules authored either in a compact declarative notation or in Lua, and with two independent stochastic controls — rule mutation and cell mutation — that let a run drift through rule space while it evolves. Every run is reproducible from a serialised session: initial state, rule, seeds, mutation schedule.

The name was confirmed on 2026-09-11 (see D-009).

## Quick start

Not yet buildable. Phase 1 is unstarted — see [ROADMAP.md](ROADMAP.md).

Once Phase 1 lands, the intended shortest path will be:

```bash
git clone https://github.com/Darian-Frey/aether.git
cd aether
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/aether --rule B3/S23 --size 512x512
```

## Build requirements

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | C++20 | GCC 12+ or Clang 15+ |
| CMake | 3.20+ | |
| raylib | 5.x | Window, input, GL context |
| rlImGui + Dear ImGui | current | Control panel |
| Lua | 5.4 | Rule scripting front end (Phase 4) |
| OpenGL | 4.3 core | Compute shaders are mandatory, not optional |

The OpenGL 4.3 requirement is load-bearing. Aether has no fallback renderer; see D-001.

## Project structure

```
aether/
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

`BUILD.md` is deliberately absent until the first successful build, per the standard's creation order (step 8). Until then this README's build requirements table is the single source of truth.

## Licence

Apache-2.0. See [LICENSE](LICENSE).
