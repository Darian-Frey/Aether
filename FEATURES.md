# Features

Capability register for Aether. IDs are append-only; withdrawn features keep their ID and gain `Status: Withdrawn`.

Priorities: Must / Should / Could / Won't.
Status: Not started | In progress | Complete | Withdrawn.

## Target users

People who want to explore cellular automata rather than run one specific automaton — the audience for whom the interesting object is rule space itself, not Conway's Life. Secondary audience: anyone wanting an ambient generative display, in the spirit of the CA screensavers that motivated the project.

## Out of scope

- Networked or distributed simulation across machines.
- Non-cubic lattices (hexagonal, triangular, Penrose). The neighbourhood model in SPEC §3 assumes an axis-aligned integer lattice throughout.
- Agent-based automata where a mobile head carries state (Langton's ant, turmites). These are not lattice-uniform and would need a second engine. Recorded as a candidate below, not a commitment.
- Reaction-diffusion PDE solvers. Adjacent, genuinely different numerics.
- Hashlife or any acceleration structure exploiting pattern periodicity — see D-008.
- Editing rules while a run is in flight without a recompile. Rule changes always go through the compiler.

## Engine

### F-001 Discrete 2D rule engine
**Priority:** Must
**Acceptance:**
- Runs any binary Life-like rule expressed in B/S notation on a 2D lattice
- Runs multi-state generations rules (Brian's Brain, cyclic CA, Wireworld) with up to 256 states
- Moore and von Neumann neighbourhoods at radius 1 and 2
- Selectable boundary condition: toroidal wrap, fixed zero, mirror
**Status:** Not started

### F-002 CPU reference implementation
**Priority:** Must
**Acceptance:**
- Every rule executable on CPU as well as GPU
- CPU and GPU produce bit-identical grids after 1000 generations for every rule in the bundled library, with cell mutation both off and on
- Selectable at runtime by flag, not compile time
**Status:** In progress
**Progress:** `sim/cpu_step` executes every table-backed kind on 1D/2D/3D grids under all three boundaries (2026-09-11). Equivalence test and runtime flag pending.
**Notes:** Exists to make AV-007 detectable. Not a performance path.

### F-003 GPU compute stepping
**Priority:** Must
**Acceptance:**
- Grid resident in GPU texture memory; ping-pong pair swapped per generation
- No per-generation host readback during a free-running simulation
- 1024×1024 binary 2D grid steps at ≥ 200 generations/second on the target machine
**Status:** Not started

### F-004 3D lattice support
**Priority:** Must
**Acceptance:**
- Cubic lattices up to at least 256³ on 4 GB VRAM
- 3D Moore (26) and von Neumann (6) neighbourhoods
- Same rule IR and same compute path as 2D; dimensionality is an IR field, not a separate engine
**Status:** Not started

### F-005 1D elementary automata
**Priority:** Should
**Acceptance:**
- Wolfram rules 0–255 by number
- Rendered as a space-time diagram: one generation per raster row, scrolling
**Status:** Not started

### F-006 Continuous-state automata
**Priority:** Could
**Acceptance:**
- Float cell states with a convolution kernel and a growth function (SmoothLife, Lenia)
- Kernel authored as a radial profile or as an explicit matrix
**Status:** Not started
**Notes:** Phase 5. The IR must accommodate float states from day one even though this feature is late — see D-010.

## Rule authoring

### F-007 Rule DSL
**Priority:** Must
**Acceptance:**
- B/S notation parses (`B3/S23`)
- Generations notation parses (`B2/S/C3`)
- Explicit transition-table blocks parse for rules the shorthand cannot express
- Syntax errors report line and column, and never leave the engine in a half-updated state
**Status:** In progress
**Progress:** DSL parses all three notations to a validated IR with line/column errors (2026-09-11); signature literals for non-totalistic blocks not yet specified.

### F-008 Lua rule scripting
**Priority:** Should
**Acceptance:**
- A Lua script returns a rule IR table and is executed exactly once, at compile time
- Sandboxed: no `io`, `os`, `require`, or filesystem access
- Instruction-count budget enforced; a script exceeding it is aborted with a diagnostic
**Status:** Not started

### F-009 Rule IR and compiler backends
**Priority:** Must
**Acceptance:**
- Both front ends emit the same IR structure (SPEC §4)
- Backend selection is automatic from IR shape: lookup table below the size threshold, generated GLSL above it
- Backend choice is visible in the UI but never a user decision
- A rule compiled through either backend produces identical results
**Status:** In progress
**Progress:** IR, validation, hash, table layout, and the LUT backend with automatic selection exist (2026-09-11); codegen backend is Phase 4.

### F-010 Rule library
**Priority:** Should
**Acceptance:**
- Bundled named rules covering each supported family, loadable by name
- User rules savable to and loadable from `rules/`
**Status:** Not started

## Initial state

### F-011 Drawing canvas
**Priority:** Must
**Acceptance:**
- Paint cells directly with a per-state brush, adjustable radius
- Works on a paused or running simulation
- In 3D, painting operates on a selectable axis-aligned slice
**Status:** Not started

### F-012 RLE pattern import
**Priority:** Should
**Acceptance:**
- Standard Life RLE files import, including the `#r`/`rule=` header
- Imported pattern is placeable by cursor before being committed to the grid
**Status:** Not started

### F-013 Random seeding
**Priority:** Must
**Acceptance:**
- Fill grid randomly with per-state density weights
- Seeded from the session RNG so the same seed reproduces the same fill
**Status:** Not started

## Dynamics

### F-014 Simulation rate control
**Priority:** Must
**Acceptance:**
- Target generations/second set independently of frame rate, via an accumulator
- Single-step, pause, and burst (run N generations as fast as possible, then stop)
- Multiple generations per frame when the target rate exceeds the frame rate
**Status:** Not started

### F-015 Rule mutation
**Priority:** Must
**Acceptance:**
- Every N generations, apply M point edits to the compiled rule and recompile
- N and M adjustable live
- Mutations never produce an IR that violates its own invariants (state indices in range, table fully populated)
- Drawn from a dedicated RNG stream so that toggling F-016 does not change the rule sequence
**Status:** Not started

### F-016 Cell mutation
**Priority:** Must
**Acceptance:**
- Per-cell probability p that the post-rule state is replaced by a uniformly random state
- Evaluated inside the compute step with no measurable throughput cost at p = 0
- Drawn from a dedicated RNG stream, hashed from cell coordinate and generation index so it is reproducible and order-independent
**Status:** Not started

### F-017 Rule lineage log
**Priority:** Must
**Acceptance:**
- Every rule the run has passed through is recorded with the generation index at which it took effect
- Any entry can be pinned (named and saved to the rule library) or rewound to (restores that rule and, optionally, the grid state)
- Log survives session save/load
**Status:** Not started
**Notes:** Without this, F-015 produces interesting rules and immediately loses them. Treated as part of the mutation feature, not an extra.

## Presentation

### F-018 2D rendering
**Priority:** Must
**Acceptance:**
- State index mapped to colour through an editable palette
- Pan and zoom with pixel-exact display at 1:1
- Optional state-age shading for generations rules
**Status:** Not started

### F-019 3D rendering
**Priority:** Must
**Acceptance:**
- Volume raymarch through the state texture with per-state colour and opacity
- Orbit camera with adjustable clipping planes and slice view
- Holds ≥ 30 fps at 256³ on the target machine
**Status:** Not started

## Session and export

### F-020 Session serialisation
**Priority:** Must
**Acceptance:**
- A saved session replays to a bit-identical grid at any generation index
- Session records initial state, rule IR, both RNG seeds, and the mutation schedule (SPEC §11)
- Format version field present from the first release
**Status:** Not started

### F-021 Frame export
**Priority:** Should
**Acceptance:**
- Export current view as PNG
- Export a numbered frame sequence over a generation range for offline encoding
**Status:** Not started

### F-022 Headless mode
**Priority:** Could
**Acceptance:**
- Run a session file for N generations with no window and dump the final grid or a frame sequence
**Status:** Not started
**Notes:** Makes CPU/GPU equivalence testing (F-002) scriptable in CI.

## Candidate features (uncommitted)

- Screensaver mode: cycle rules on a timer with no UI chrome, as an X screensaver or standalone fullscreen binary. This is where the original motivation came from and it may deserve promotion to a Should.
- Agent-based automata (Langton's ant, turmites) behind a second engine.
- Fitness-directed rule search: score each mutated rule on population entropy or activity and keep the branches that score well, turning F-015 from a random walk into a search. The lineage log (F-017) is already the substrate this would need.
- Hexagonal lattice support.
- Rule diffing: show what changed between two lineage entries.
- Audio-reactive parameter modulation.
