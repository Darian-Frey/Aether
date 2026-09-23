> **Status:** Active
> **Provenance:** Shane Hartley (author); Claude (documentation scaffold, 2026-08-30)
> **Last reviewed:** 2026-09-23
> **Why this status:** Phases 1–5 complete (2D and 3D discrete core, hex lattices, mutation, lineage, sessions, Lua, codegen, rule library, continuous states); Phase 6 (presentation and release) under way — patterns and the editor are done, the release checklist is not.

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

### In the window

Pause, step, burst and the rate live in the bar across the top, with the generation count and the running rule beside them. The left column holds everything else in sections, closed until you want them; **Keys** lists the shortcuts, and F1 opens it.

| | |
|---|---|
| Left drag | Paint with the brush (in 3D, on the current slice) |
| Right drag | Pan in 2D, orbit in 3D |
| Wheel | Zoom |
| Space · N | Pause · single step |
| R · C · F | Random fill · clear · fit the view |
| `[` `]` · 0–9 | Brush radius · brush state |
| S · `,` `.` | 3D: slice mode · move the slice |

### Rules

Type a rule into the Rule panel and compile it with Ctrl+Enter. Four notations:

```
B3/S23                              Life-like
B2/S/C3                             Generations
states 2; neighbourhood hex 1;      a table block: count conditions,
  0: n(1) == 2 -> 1;                signature literals like [1, _, 2, _] rot,
  1: n(1) < 3 or n(1) > 4 -> 0;     and decay N for an ageing tail
```

Or pick Lua in the same panel, or pass `--lua rule.lua`: a script runs once, at compile time, and returns a table describing the rule, computing the transition rather than tabulating it.

The Library section lists the bundled rules — Life, HighLife, Seeds, Day & Night, Diamoeba, Brian's Brain, Star Wars, Wireworld, a cyclic CA, a hexagonal Life, two of Bays' 3D rules and a fading Life. `--rule @wireworld` loads one by name, and rules you write save back into `rules/`.

### Patterns and seeding

The Patterns section lists what is in `patterns/` — gliders, a Gosper gun, a Wireworld loop, a Brian's Brain glider, a cyclic spiral seed, a hex oscillator and a 3D shell — or opens any `.rle` or `.pattern` file. A chosen pattern follows the cursor until you click, drawn in the colours it will become rather than written into the grid, so you can see it against what is already there before committing to it; one the running rule cannot take is greyed in the list, tinted red under the cursor, and says why. Shift-drag the grid to select a region, and the Patterns section will write it back out as a file: Golly's extended RLE where RLE reaches, and a native `.pattern` where it does not, so hexagonal, 3D and continuous patterns are not squeezed into a format that cannot hold them.

The same selection is what **Seed region** fills, at the density weights in the Grid section and leaving everything outside it alone; **Seed** does the whole grid, and in 3D **Seed slice** does the one the brush is painting on. Every one of these is recorded, so a session replays a paste or a partial reseed exactly as it happened.

Press `E` for the pattern editor: a scratch pad with a grid and a rule of its own, drawn on a cell at a time with the same brush the grid uses. It steps forward **and back**, which the simulation cannot — a small host-side grid can afford to remember where it has been. It adopts whatever rule is running, or any bundled one, so a creature is watched under the rule it is being built for. Nothing on the pad is part of the run: it is not saved with the session and it does not disturb one. What leaves it is an ordinary pattern, either placed into the grid or written into `patterns/`.

Hovering a cell on the pad shows what it is about to do: its state and the state it becomes, every neighbour drawn in the neighbourhood's own shape, the counts the rule actually asked about, and the table entry or the condition that answered. Where a boundary sent a neighbour to the far side of the grid, or off it, the diagram says so, which is the part worth seeing — a cell on an edge behaves differently from one in the middle and it is rarely obvious how. None of these figures is worked out for the display: they come from the same function the simulation steps with, so the explanation cannot drift from the behaviour.

### Drift and reproducibility

The Mutation section has both controls: cell mutation as a probability per cell, optionally grouped into blocks so noise arrives in clumps, and rule mutation as point edits to the rule every N generations. Every rule a run passes through is in the Lineage list, where it can be pinned by name or rewound to — either the rule alone, or the grid with it. `--rule-mutation 250:1 --cell-mutation 0.0001` starts with both on.

The Export section writes what is in the viewport — the automaton, without the panels over it — as a PNG, or as a numbered sequence over a range of generations for `ffmpeg` or anything else to turn into a film. A sequence is counted in generations rather than in frames, so it is a record of the run and not of how fast this machine happened to be drawing.

The Session section saves and loads `.aether` files. A saved run replays bit-for-bit from its initial state: **Verify replay** checks it on the other execution path, and `aether replay in.aether out.aether` does the same headlessly.

The same pictures can be had with no window at all:

```bash
aether headless --rule B3/S23 --size 512x512 --generations 2000        --frame-dir frames --frame-every 10 --png final.png
ffmpeg -framerate 30 -i frames/frame_%06d.png life.mp4
```

Frames render through the same palette pass the window uses, at one pixel per cell unless `--frame-scale` says otherwise.

See [BUILD.md](BUILD.md) for prerequisites and for running on the NVIDIA GPU on an Optimus laptop.

## Build requirements

| Requirement | Version | Notes |
|---|---|---|
| C++ compiler | C++20 | GCC 12+ or Clang 15+ |
| CMake | 3.20+ | |
| raylib | 6.0 | Fetched and built in-tree with the 4.3 rlgl backend |
| rlImGui + Dear ImGui | `Raylib_6_0` / 1.92.7 | Fetched and built in-tree |
| Lua | 5.4 | Rule scripting front end; taken from the system, not fetched |
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
├── patterns/        Bundled pattern library
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
