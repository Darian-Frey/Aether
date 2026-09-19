# Features

Capability register for Aether. IDs are append-only; withdrawn features keep their ID and gain `Status: Withdrawn`.

Priorities: Must / Should / Could / Won't.
Status: Not started | In progress | Complete | Withdrawn.

## Target users

People who want to explore cellular automata rather than run one specific automaton — the audience for whom the interesting object is rule space itself, not Conway's Life. Secondary audience: anyone wanting an ambient generative display, in the spirit of the CA screensavers that motivated the project.

## Out of scope

- Networked or distributed simulation across machines.
- ~~Non-cubic lattices (hexagonal, triangular, Penrose). The neighbourhood model in SPEC §3 assumes an axis-aligned integer lattice throughout.~~ Narrowed 2026-09-11 (D-012): hexagonal is in scope as F-023; triangular and Penrose are candidates below. Lattices without integer coordinates remain out of scope for this engine.
- Agent-based automata where a mobile head carries state (Langton's ant, turmites). These are not lattice-uniform and would need a second engine. Recorded as a candidate below, not a commitment.
- Reaction-diffusion PDE solvers. Adjacent, genuinely different numerics.
- Hashlife or any acceleration structure exploiting pattern periodicity — see D-008.
- Editing rules while a run is in flight without a recompile. Rule changes always go through the compiler.
- Cells writing to one another: feeding as an energy transfer, movement between sites, and energy shared between cells. The step is a gather — every invocation writes only its own cell — and these are scatters. Recorded 2026-09-16 by D-019, which also records the two routes back if they are ever wanted.

## Engine

### F-001 Discrete 2D rule engine
**Priority:** Must
**Acceptance:**
- Runs any binary Life-like rule expressed in B/S notation on a 2D lattice
- Runs multi-state generations rules (Brian's Brain, cyclic CA, Wireworld) with up to 256 states
- Moore and von Neumann neighbourhoods at radius 1 and 2
- Selectable boundary condition: toroidal wrap, fixed zero, mirror
**Status:** Complete
**Progress:** 2026-09-11. B/S, Generations and count-condition table blocks; Moore and von Neumann at any radius (r=1,2 exercised); wrap/zero/mirror on both paths.

### F-002 CPU reference implementation
**Priority:** Must
**Acceptance:**
- Every rule executable on CPU as well as GPU
- CPU and GPU produce bit-identical grids after 1000 generations for every rule in the bundled library, with cell mutation both off and on
- Selectable at runtime by flag, not compile time
**Status:** In progress
**Progress:** CPU oracle for every table kind; equivalence suite of 15 rules × 3 boundaries × 1000 generations on both GPUs; runtime path switch in the UI and `--cpu` flag (2026-09-11). Cell mutation is on both paths and in the equivalence suite (2026-09-11). Remaining: the bundled library as the fixture set (Phase 4).
**Notes:** Exists to make AV-007 detectable. Not a performance path.

### F-003 GPU compute stepping
**Priority:** Must
**Acceptance:**
- Grid resident in GPU texture memory; ping-pong pair swapped per generation
- No per-generation host readback during a free-running simulation
- 1024×1024 binary 2D grid steps at ≥ 200 generations/second on the target machine
**Status:** Complete
**Progress:** 2026-09-11. GPU-resident pair, no per-generation readback, 3,684 gen/s at 1024² Life on the T1200.

### F-004 3D lattice support
**Priority:** Must
**Acceptance:**
- Cubic lattices up to at least 256³ on 4 GB VRAM
- 3D Moore (26) and von Neumann (6) neighbourhoods
- Same rule IR and same compute path as 2D; dimensionality is an IR field, not a separate engine
**Status:** Complete
**Progress:** 2026-09-12. 3D grids up to the VRAM guard's limit (256³ is 33.5 MB); Moore 26 and von Neumann 6; same IR and compute path as 2D; 49 gen/s at 256³ with rendering on the T1200.

### F-005 1D elementary automata
**Priority:** Should
**Acceptance:**
- Wolfram rules 0–255 by number
- Rendered as a space-time diagram: one generation per raster row, scrolling
**Status:** Not started

### F-006 Continuous-state automata
**Priority:** Could
**Acceptance:**
- Float cell states with a convolution kernel and a growth function (Lenia; a kernel carries one profile, which is what puts SmoothLife outside it — D-021)
- Kernel authored as a radial profile or as an explicit matrix
**Status:** Complete
**Progress:** 2026-09-17 (Phase 5). The `f32` grid path holds, saves and loads a float grid; Lua authors a continuous rule — a kernel as samples the script works out with `math`, and a named growth function lowered by `rule/growth` as `decay` lowers an ageing tail; and the CPU path steps one: convolve, grow, clamp, with the profile resolved onto the offsets and normalised by `rule/kernel` (D-020). A continuous rule sustains a self-limiting structure over hundreds of generations. 2026-09-17: the GPU path too — `aether_rule_f` from the growth expression and `continuous_step.comp` for the convolution — with the two paths bitwise identical over 1000 generations under every boundary, and a 128² configuration identical after 10,000. 2026-09-17: `f32` cells through the palette, on a monotone ramp rather than the hue cycle a state count gets, and BUG-011 — a run that never synchronises comes back wrong — found and fixed at the size the acceptance asks for. Complete 2026-09-19.
**Notes:** Phase 5. The IR must accommodate float states from day one even though this feature is late — see D-010.

### F-023 Hexagonal lattice
**Priority:** Should
**Acceptance:**
- `neighbourhood hex r` in the DSL and `NeighbourhoodType::Hexagonal` in the IR, N = 3r(r+1), same canonical ordering rule as SPEC §3
- Every table kind, both execution paths, all three boundaries, with hexagonal fixtures in the equivalence suite
- Rendered as a hex tiling with pan and zoom; painting lands on the hex under the cursor
- Rule and cell mutation unchanged
**Status:** Complete
**Progress:** 2026-09-12. Axial storage, six fixed offsets, N = 3r(r+1); both paths and all boundaries via three hex equivalence fixtures; hex tiling rendered as a rhombus with painting landing on the hex under the cursor; mutation unchanged.
**Notes:** Added 2026-09-11 by D-012. Axial coordinates on the existing square storage; nothing in `core/` or `sim/` changes.

## Rule authoring

### F-007 Rule DSL
**Priority:** Must
**Acceptance:**
- B/S notation parses (`B3/S23`)
- Generations notation parses (`B2/S/C3`)
- Explicit transition-table blocks parse for rules the shorthand cannot express
- Syntax errors report line and column, and never leave the engine in a half-updated state
**Status:** Complete
**Progress:** 2026-09-14. Errors carry line and column and leave the engine untouched. All three notations parse, including signature literals with wildcards and `rot` for non-totalistic rules (IMP-002), and `decay N;` for an ageing tail (F-025).

### F-008 Lua rule scripting
**Priority:** Should
**Acceptance:**
- A Lua script returns a rule IR table and is executed exactly once, at compile time
- Sandboxed: no `io`, `os`, `require`, or filesystem access
- Instruction-count budget enforced; a script exceeding it is aborted with a diagnostic
**Status:** Complete
**Progress:** 2026-09-14. `rule/lua` runs a script once per compile in a fresh interpreter with its own environment table, an instruction budget and a memory budget. A transition may be an array in layout order or a function the host calls once per table entry, which is what makes a large rule practical to write. Language selector in the Rule panel and `--lua FILE`.

### F-009 Rule IR and compiler backends
**Priority:** Must
**Acceptance:**
- Both front ends emit the same IR structure (SPEC §4)
- Backend selection is automatic from IR shape: lookup table below the size threshold, generated GLSL above it
- Backend choice is visible in the UI but never a user decision
- A rule compiled through either backend produces identical results
**Status:** Complete
**Progress:** 2026-09-15. Both front ends emit the IR; both backends consume it; selection is automatic from IR shape and shown as metadata. A rule expressible both ways gives identical output through either, which the equivalence suite checks under every boundary.

### F-010 Rule library
**Priority:** Should
**Acceptance:**
- Bundled named rules covering each supported family, loadable by name
- User rules savable to and loadable from `rules/`
- The bundled set includes at least these classics, with provenance noted where an xscreensaver/xlockmore hack is the reference (2026-09-12):
  - Life-like: Conway's Life `B3/S23`, HighLife `B36/S23`, Seeds `B2/S`, Day & Night `B3678/S34678`, Diamoeba `B35678/S5678` (xlock `life`)
  - Generations: Brian's Brain `B2/S/C3`, Star Wars `B2/S345/C4`
  - Cyclic: Griffeath's cyclic CA, 8 and 14 states (xscreensaver `demon`)
  - Wireworld
  - Langton's self-reproducing loops (xscreensaver `loop`; 8 states, von Neumann, non-totalistic — writable since 2026-09-14 now that signature literals exist, and small enough for the table backend at 32,768 entries; the 219 transitions themselves still have to be transcribed or computed in Lua)
  - 1D: Rules 30, 90, 110 (xscreensaver `life1d`; with F-005)
  - 3D: Bays' 5766 and 4555 (xlock `life3d`; with F-004)
  - Hexagonal: a hex Life-like such as `B2/S34` (F-023)
- Each bundled rule carries its palette and a one-line description
**Status:** In progress
**Progress:** 2026-09-14. Fourteen rules bundled in `rules/`, loadable by name from the Library panel or `--rule @id`, each with a description and, where the default is wrong, a palette. A rule file is its own source with a comment header, so the header is inert in both the DSL and Lua and the file is compiled whole. User rules save to `rules/`. A test compiles every bundled rule and checks it validates.
**Remaining:** Langton's loops (219 transitions to transcribe) and the 1D rules (they need F-005's space-time view to be worth looking at, and the app has no 1D grid yet). The 14-state cyclic CA joined the library on 2026-09-14 once D-016 brought it down to 126 entries.
**Notes:** `voters` and `dilemma` from xscreensaver are *not* on the list: the first needs a stochastic transition form and the second a two-phase or radius-2 rule; see Candidate features.

## Initial state

### F-011 Drawing canvas
**Priority:** Must
**Acceptance:**
- Paint cells directly with a per-state brush, adjustable radius
- Works on a paused or running simulation
- In 3D, painting operates on a selectable axis-aligned slice
**Status:** Complete
**Progress:** 2026-09-12. 2D brush on square and hex lattices; in 3D, painting on the selected axis-aligned slice in slice mode, picked by ray–slab intersection. All painting through `paintSpan` with no readback.

### F-012 Pattern import and export
**Priority:** Should
**Acceptance:**
- Standard Life RLE files import, including the `#r`/`rule=` header
- Golly's extended RLE imports and exports for multi-state 2D square patterns, so Generations, cyclic and Wireworld creatures pass between Aether and other tools
- A native format carries what no RLE dialect describes: hexagonal lattices, 3D patterns and any state count, sharing the session cell codec
- Imported pattern is placeable by cursor before being committed to the grid, and placement journals itself like any other grid mutation, so a session replays a pasted pattern
- A selected region of the grid exports to a file: extended RLE where the pattern fits it, the native format otherwise, with the choice reported rather than silent
- A pattern whose lattice or state count does not fit the current grid is refused with a diagnostic, never coerced
**Status:** Not started
**Notes:** Widened 2026-09-15 by D-017 from import-only to import and export across two formats. As originally written the entry named standard RLE alone, which describes two states on a square lattice and so could not carry patterns for most of the bundled rules.

### F-013 Random seeding
**Priority:** Must
**Acceptance:**
- Fill grid randomly with per-state density weights
- Seeded from the session RNG so the same seed reproduces the same fill
**Status:** Complete
**Progress:** 2026-09-11. Per-state densities, one stream-A draw per cell, `--seed` reproduces the fill.

### F-027 Bundled pattern library
**Priority:** Should
**Acceptance:**
- Named patterns bundled in `patterns/`, listed in the interface and placeable into the grid by cursor
- Searched at run time the way `rules/` is, with `$AETHER_PATTERNS` taking precedence
- Each pattern names the rule it is meant for, with a description and its provenance
- The bundled set covers the families the rule library already ships: Life spaceships and a gun, a Brian's Brain oscillator, a Wireworld circuit, a cyclic-CA seed, a hexagonal pattern, and a 3D pattern for Bays' rules
- User patterns save back to `patterns/` from the same panel, as user rules do to `rules/`
- A test loads every bundled pattern and checks it parses and fits the rule it names, as the rule-library test does
**Status:** Not started
**Notes:** Added 2026-09-15 by D-017. This is the pattern counterpart of F-010, and depends on F-012 for both formats. `patterns/` has existed empty since the tree was created and the README has advertised it since; this is the entry that fills it.

### F-028 Region seeding
**Priority:** Should
**Acceptance:**
- A dragged rectangle seeds only the cells inside it, at the density weights of F-013, leaving the rest of the grid untouched
- Drawn from stream A like the whole-grid fill and journalled with its bounds, so a session replays it exactly
- Works on both execution paths and every lattice; in 3D it acts on the painting slice, as F-011 does
- The whole-grid fill keeps its control and its `R` shortcut
**Status:** Not started
**Notes:** Added 2026-09-15 by D-017. Region seeding and pattern placement are the same gesture from opposite directions: put something into part of the grid, from the random side or from the library. The related complaint — that the existing whole-grid Seed button is hard to find beneath the density sliders — is a presentation change rather than a feature, logged as IMP-004.

### F-029 Pattern editor
**Priority:** Should
**Acceptance:**
- A scratch-pad grid, sized independently of the simulation, painted one cell at a time at any state with the F-011 brush
- Steps forward and back a generation at a time on the CPU path, independently of the live simulation, which carries on or stays paused as it was
- Adopts the live simulation's rule by default, with any bundled rule selectable instead
- Saves to and loads from `patterns/` in the formats of F-012
- Its contents place into the live grid through F-012's placement path, journalled like any other grid mutation
- Runs with no GL context, so it is testable headlessly
**Status:** Not started
**Notes:** Added 2026-09-15 by D-018. Deliberately outside the session and the journal: the scratch pad is not part of the run, so it neither replays nor perturbs replay. Stepping backwards is affordable only because the grid is small and host-side — a history ring is nothing here and would be unthinkable on a 256³ grid.

### F-030 Cell inspector
**Priority:** Should
**Acceptance:**
- For the cell under the cursor: its state, every neighbour's state laid out in the neighbourhood's own geometry, the counts the rule actually asks about, the table entry or expression clause that fires, and the state it becomes
- Every figure comes from the oracle's per-cell entry point; the inspector never derives a transition of its own
- Neighbours are resolved through `sim::resolve`, the same boundary handling the step uses, so a cell on an edge or in a corner explains correctly
- Covers all four table kinds and the expression form
- A test asserts the inspector's predicted next state equals what `cpuStep` writes, for every cell of every equivalence fixture under every boundary
**Status:** Not started
**Notes:** Added 2026-09-15 by D-018, as the inspecting half of the editor. Depends on IMP-005. Built for the scratch pad of F-029, but nothing in it is specific to that grid — pointing it at a running simulation is a readback problem, not an inspector problem, and is the reversal condition recorded in D-018.

## Dynamics

### F-014 Simulation rate control
**Priority:** Must
**Acceptance:**
- Target generations/second set independently of frame rate, via an accumulator
- Single-step, pause, and burst (run N generations as fast as possible, then stop)
- Multiple generations per frame when the target rate exceeds the frame rate
**Status:** Complete
**Progress:** 2026-09-11. Accumulator, pause, single-step, burst, per-frame cap and wall-clock budget with a below-target indicator in the UI.

### F-015 Rule mutation
**Priority:** Must
**Acceptance:**
- Every N generations, apply M point edits to the compiled rule and recompile
- N and M adjustable live
- Mutations never produce an IR that violates its own invariants (state indices in range, table fully populated)
- Drawn from a dedicated RNG stream so that toggling F-016 does not change the rule sequence
**Status:** Complete
**Progress:** 2026-09-11. `sim::mutateRule` draws point edits from stream A, validates, redraws up to 8 times; `Simulation::maybeMutateRule` fires every `interval` generations before the step; N and M live in the UI. The rule sequence is unchanged by toggling cell mutation (tested).

### F-016 Cell mutation
**Priority:** Must
**Acceptance:**
- Per-cell probability p that the post-rule state is replaced by a uniformly random state
- Evaluated inside the compute step with no measurable throughput cost at p = 0
- Drawn from a dedicated RNG stream, hashed from cell coordinate and generation index so it is reproducible and order-independent
**Status:** Complete
**Progress:** 2026-09-11. Hashed from coordinate, generation and seed B on both paths; no measurable cost at `p = 0`; equivalence suite runs with `p = 0.02` across every fixture and boundary.

### F-017 Rule lineage log
**Priority:** Must
**Acceptance:**
- Every rule the run has passed through is recorded with the generation index at which it took effect
- Any entry can be pinned (named and saved to the rule library) or rewound to (restores that rule and, optionally, the grid state)
- Log survives session save/load
**Status:** Complete
**Progress:** 2026-09-12. Append on every rule change, pin by name, rule-only rewind (appends) and grid rewind (replay, truncates), survives save/load with delta-encoded entries.
**Notes:** Without this, F-015 produces interesting rules and immediately loses them. Treated as part of the mutation feature, not an extra.

## Ecosystem

Added 2026-09-16 by D-019, from `docs/ecosystem-design-note.md`. Every feature here keeps the engine's gather model: a cell reads its neighbourhood and the fields at its own site, and writes only itself. Feeding, movement between sites and energy transfer between cells are not here, and D-019 records why.

### F-031 Multi-field grids
**Priority:** Should
**Acceptance:**
- A grid may carry more than one field per site — the state plus any number of declared auxiliary fields, each with its own cell type
- Each field is stored as its own texture, so the state field keeps the layout and the storage it has today
- A rule reads any field at its own site and at its neighbours, and writes only fields at its own site
- Field declarations live in the IR; a session records every field, and a session naming one field loads unchanged
- Both execution paths, with a multi-field fixture in the equivalence suite
**Status:** Not started
**Notes:** Added 2026-09-16 by D-019 as the substrate the rest of this section rests on. Chosen over widening the cell into a record because it is additive: SPEC §1 still says a cell holds one value, and what gained a dimension is the site rather than the cell.

### F-032 Abiotic resource field
**Priority:** Should
**Acceptance:**
- A scalar `f32` field seeded as a patchy noise field rather than uniformly, since uniform resources produce uniform populations
- Each step, regeneration toward a per-site carrying capacity at a settable rate, with optional diffusion
- A rule may read the resource at its own site and its neighbours', and draw down the resource at its own site
- The regeneration rate is exposed as the primary harshness control, with a minimum seed rate available as damping
- Conservation is observable: what enters by regeneration and leaves by consumption is counted and reconcilable (AV-018)
**Status:** Not started
**Notes:** Added 2026-09-16 by D-019. Depends on F-031 and on Phase 5's `f32` grid path, which is where the float field machinery comes from. Sessile resource-feeding rules — the design note's plants — need nothing beyond this and F-031, because a plant drawing on the resource at its own site is an ordinary gather; what plants cannot do is be grazed, which would be a write to another cell.

### F-033 Per-cell genome with inheritance
**Priority:** Should
**Acceptance:**
- A genome field holding a rule the cell runs — bounded to what a shader can interpret cheaply, a Life-like B/S bitmask being the reference case
- At birth the child's genome is derived from its live neighbours by majority vote per gene, by random parent, or by crossover, with a per-gene mutation probability applied afterwards
- Every draw comes from stream B, hashed on coordinate and generation, so a run replays bit-identically
- Grouped and per-cell modes, as F-026 provides for cell mutation
- Cells are colourable by genome hash, so lineages are visible spreading and dying out
- `selectBackend` routes a rule reading a genome field to codegen whatever its size, since no single table can serve a grid of differing rules
**Status:** Not started
**Notes:** Added 2026-09-16 by D-019. The design note's "the genome is the rule" is unimplementable in general — a million cells would be a million rules to compile — but a Life-like genome is eighteen bits and the rule that reads it is one shift and one mask, which the codegen backend already emits. This is the third mutation control, after F-015 over time and F-016 over space: variation that is inherited, and therefore selected rather than merely applied.

### F-034 Hard cell lifespan
**Priority:** Could
**Acceptance:**
- An optional maximum age after which a cell dies regardless of its neighbours, alongside the soft decay of F-025 rather than instead of it
- Expressed by the same desugaring route as F-025, so nothing downstream sees a new concept
- Genome-tunable where F-033 is in use
**Status:** Not started
**Notes:** Promoted from a candidate on 2026-09-16 by D-019, having been set aside on 2026-09-14 by D-014 in favour of soft decay. The design note asks for it directly, and the two are not alternatives: a tail gives a cell somewhere to fade to, a lifespan gives it a deadline. Fertility windows and juvenile periods need no feature at all — a cell's age is already its state index within the tail, so they are ordinary conditions over those states.

### F-035 Similarity-biased birth
**Priority:** Could
**Acceptance:**
- Where several parents could produce a birth, the outcome is weighted by the genetic similarity of the candidate site's live neighbours
- Decided entirely by the cell being born, from what it can see, so the rule remains a gather
- Reproducible from stream B like every other stochastic element
**Status:** Not started
**Notes:** Added 2026-09-16 by D-019, from §5.1 of the design note, which is careful to note that it stays a strict cellular automaton. Clustering by genome emerges from where births land rather than from anything moving, which is what makes it expressible here at all; the movement-based form in §5.2 is what D-019 refuses.

### F-036 Population and field readouts
**Priority:** Should
**Acceptance:**
- Population over time per state, per genome or per clan tag, plotted as the run proceeds
- Totals per field, including the conservation figure AV-018 needs
- Computed as GPU reductions; no per-step host readback of the grid at any point
- Present on both execution paths and recorded in the session where they are parameters rather than observations
**Status:** Not started
**Notes:** Added 2026-09-16 by D-019. The design note wants these to watch predator–prey oscillation; they are just as necessary for watching a genome sweep a grid under F-033. The readback constraint is not an optimisation — a population graph fed by a per-generation `glGetTexImage` would reintroduce AV-002 permanently, in the one place it would never be noticed as a cause.

## Presentation

### F-018 2D rendering
**Priority:** Must
**Acceptance:**
- State index mapped to colour through an editable palette
- Pan and zoom with pixel-exact display at 1:1
- Optional state-age shading for generations rules
**Status:** Complete
**Progress:** 2026-09-11. Palette lookup, editable per state in the UI, pan and zoom pixel-exact at integer zoom, age shading toggle.

### F-019 3D rendering
**Priority:** Must
**Acceptance:**
- Volume raymarch through the state texture with per-state colour and opacity
- Orbit camera with adjustable clipping planes and slice view
- Holds ≥ 30 fps at 256³ on the target machine
**Status:** Complete
**Progress:** 2026-09-12. Voxel-exact raymarch with per-state colour and opacity from the palette; orbit camera; per-axis clip ranges; single-slice mode; 49 fps at 256³ on the T1200.

### F-025 Cell life cycle
**Priority:** Should
**Acceptance:**
- `decay N;` in a table block gives any rule an ageing tail: a cell the rule would kill fades through N states, one per generation, before reaching 0
- Tail states count as quiescent, so a fading cell neither feeds a birth nor supports a survival
- Equivalent to the Generations shorthand where both can express a rule: `B2/S/C3` and the same rule written longhand with `decay 1` compile to the same table
- The tail is coloured as a ramp by default, fading in colour and in 3D opacity as a cell ages; age shading darkens only the tail
- Works on every lattice and both execution paths, with a decayed rule in the equivalence fixture set
**Status:** Complete
**Progress:** 2026-09-14 (D-014). A front-end desugaring in `rule/decay`: nothing downstream changes. The table-size cap that limited a tail to six states was lifted the same day by D-016; a tail now runs to the 256-state limit of SPEC §1 on every lattice.

### F-026 Correlated cell mutation
**Priority:** Should
**Acceptance:**
- A block size for cell mutation: every cell in an aligned block of `2^k` per axis shares the decision to mutate while drawing its own replacement state
- `k = 0` is identical to the per-cell form, bit for bit, so earlier sessions replay unchanged
- `p` keeps its meaning at every block size: the expected fraction of cells changed per generation
- Reproducible and order-independent on both paths, covered by the equivalence suite
- Recorded in the session and journal
**Status:** Complete
**Progress:** 2026-09-14 (D-015). Block shift in `CellMutation`, twinned in `shaders/hash.glsl`; slider in the Mutation panel and `--cell-mutation P:K`.

### F-024 Screensaver mode
**Priority:** Should
**Acceptance:**
- A fullscreen mode with no UI chrome that cycles through a playlist of bundled rules on a timer, reseeding each
- Optional rule mutation and cell mutation per playlist entry, so a run drifts rather than repeats
- Exits on any input; usable as a standalone fullscreen binary, and as an X screensaver hack via the `XSCREENSAVER_WINDOW` convention if that proves practical
- Session reproducibility unaffected: every run it shows is saveable
**Status:** Not started
**Notes:** Promoted from Candidate features 2026-09-12; this is the project's original motivation. Phase 6.

## Session and export

### F-020 Session serialisation
**Priority:** Must
**Acceptance:**
- A saved session replays to a bit-identical grid at any generation index
- Session records initial state, rule IR, both RNG seeds, and the mutation schedule (SPEC §11)
- Format version field present from the first release
**Status:** Complete
**Progress:** 2026-09-12. `.aether` JSON per SPEC §11 with a journal of user actions; replays bit-identically on either path across processes (`ctest -R replay`); format version checked on load.

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
**Status:** In progress
**Progress:** `aether headless`, `aether replay` and `aether compare` exist for the cross-process replay test (2026-09-12); frame-sequence dump pending.
**Notes:** Makes CPU/GPU equivalence testing (F-002) scriptable in CI.

## Candidate features (uncommitted)

- ~~Screensaver mode.~~ Promoted to F-024 (2026-09-12).
- Stochastic transition form: rules where a cell's next state is drawn at random from its neighbours or by a per-rule probability — the voter model (xscreensaver `voters`), forest fire, Ising-like dynamics. Not expressible today: cell mutation is the engine's only randomness. Would need a new `Kind` and transition form in the IR, draws from a stream-B-style hash so both paths agree, and a D-entry (2026-09-12).
- Two-phase or score-based rules: spatial prisoner's dilemma (xscreensaver `dilemma`) scores each cell against its neighbours and then copies the best-scoring neighbour's strategy, which is a radius-2 non-totalistic rule with a 2²⁴-entry table — codegen only (Phase 4), or a two-pass step the engine does not have (2026-09-12).
- Agent-based automata (Langton's ant, turmites) behind a second engine.
- Fitness-directed rule search: score each mutated rule on population entropy or activity and keep the branches that score well, turning F-015 from a random walk into a search. The lineage log (F-017) is already the substrate this would need.
- Triangular lattice: representable on the square storage with two offset lists selected by the parity of x + y. Bounded but bends the uniform-lattice assumption both steppers and the table index rely on (D-012).
- Penrose or other aperiodic lattices: no integer coordinates, so cells become a graph with explicit adjacency, the step a gather by index, rendering a polygon list, and cell mutation hashed by cell index. A separate graph-lattice engine, not an extension of this one (D-012).
- Structurally grouped mutation: a connected cluster of live cells mutating as a unit, rather than the spatial blocks of F-026. Needs connected-component labelling every generation, which is not a function of a cell's neighbourhood — it would take multi-pass label propagation on the GPU and would break the one-invocation-per-cell step model (D-015, 2026-09-14).
- ~~Hard cell lifespan.~~ Promoted to F-034 (2026-09-16, D-019). Set aside on 2026-09-14 in favour of soft decay (D-014); the ecosystem design note asks for it directly, and the two turn out to be complementary rather than alternatives.
- Multi-kernel rules, and SmoothLife with them: a `Kernel` carries one profile, so a growth function is a function of one convolution, and SmoothLife is a function of two — an inner disc and an outer annulus, with the thresholds applied to one filling interpolated by the other. Generalising `profile` to a list with a `Conv(i)` operator would carry it and the multi-kernel Lenia family too, at the cost of the IR schema, `rule/kernel`, the compiled rule's buffers, the shader's convolution loop and the named growth forms. Note the second blocker before starting: *smooth* SmoothLife needs `exp` for its sigmoids, which is excluded on determinism grounds (AV-015), so two kernels deliver the hard-threshold variant rather than the familiar one. Best decided alongside Phase 7's multi-field grids (D-021, 2026-09-19).
- Mutation patches: discs at a hashed centre instead of aligned blocks, for a less grid-aligned look (D-015 option B).
- Rule diffing: show what changed between two lineage entries.
- Audio-reactive parameter modulation.
