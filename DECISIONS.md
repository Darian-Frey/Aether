# Decisions

Append-only log of significant design decisions. Each entry: D-NNN with Decided and Recorded dates (ISO 8601), status, context, alternatives, decision, consequences, and reversal conditions.

Status vocabulary: Proposed | Accepted | Superseded by D-NNN | Deprecated.

---

### D-001 GPU compute shaders as the sole execution backend
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-003, F-004, AV-001, AV-002, ARCHITECTURE.md §Cross-cutting concerns

**Context.** A 256³ lattice is 16.7 million cells. Stepping that per frame on the CPU will not hold an interactive frame rate, and 3D is a Must feature rather than a stretch goal. The execution strategy had to be settled before any grid representation was chosen, because it dictates where the grid lives.

**Options.**
- **A. Multithreaded CPU stepping.** Portable, easy to debug, no GL version floor. Rejected: even with perfect scaling across 8 cores this leaves 3D an order of magnitude short, and the memory bandwidth for a full grid read-modify-write per generation is the binding constraint rather than the arithmetic.
- **B. CPU with SIMD and bitboard packing.** Very fast for binary 2D rules specifically. Rejected: the packing tricks that make it fast are rule-family-specific and collapse for multi-state and continuous automata, which is most of what this project is for.
- **C. GPU compute shaders with the grid resident in texture memory.** Chosen.

**Decision.** Option C. The grid lives in GPU textures — 2D texture for 2D lattices, 3D texture for 3D — and is stepped by a compute shader that reads one buffer and writes the other. OpenGL 4.3 core is a hard requirement with no software fallback.

**Consequences.**
- 2D rendering becomes nearly free: the state texture is already where the renderer needs it, so display is a palette lookup rather than an upload.
- 3D rendering likewise reduces to raymarching a texture that already exists.
- Rules must be expressible as GPU-executable code, which constrains the IR (D-002) and rules out data-dependent unbounded loops in rule expressions.
- Debugging is harder. Mitigated by the CPU reference path (D-003).
- The GL 4.3 floor excludes some older hardware. Accepted: the target machine is a ThinkPad P15 with an NVIDIA T1200, and the project is Linux-first and personal.

**Reversal conditions.** Revisit if (a) the target hardware changes to something without compute shader support, or (b) profiling shows host-GPU synchronisation dominating such that the GPU advantage evaporates for the grid sizes actually used.

---

### D-002 A single rule IR between front ends and backends
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-007, F-008, F-009, D-004, SPEC.md §4

**Context.** Rules are to be authorable both declaratively and in Lua (F-007, F-008), and executable both as lookup tables and as generated shader code (D-004). Two front ends times two backends is four paths if they connect directly, and four paths cannot be kept consistent.

**Options.**
- **A. Direct compilation from each front end to each backend.** Fewer layers, less indirection. Rejected: four compilation paths means four places for semantics to drift, and every new front end or backend multiplies rather than adds.
- **B. Lua as the universal intermediate — the DSL desugars to Lua.** Tempting, since Lua is already a dependency. Rejected: it makes Lua load-bearing for the common case and pulls the interpreter closer to the hot path than D-003 permits. It also makes IR validation harder, since validating a Lua program is not the same as validating a data structure.
- **C. A declarative IR that both front ends emit and both backends consume.** Chosen.

**Decision.** Option C. `rule/ir` defines a validated data structure describing dimensionality, state count, neighbourhood, boundary, and a transition specification. Front ends produce it; backends consume it; nothing else constructs one.

**Consequences.**
- New front ends and new backends both cost one implementation, not N.
- IR validation happens once, in one place, and catches malformed rules from any source including rule mutation (D-005).
- The IR is the natural unit for hashing into the session record and for the lineage log.
- The IR must be expressive enough for continuous automata even before those are implemented, or Phase 5 forces a breaking change — see D-010.

**Reversal conditions.** Revisit if the IR grows so many special cases that the backends are effectively switching on rule family anyway, at which point the abstraction is paying no rent.

---

### D-003 Lua executes at compile time only, never per cell
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-008, AV-008, AV-009, ARCHITECTURE.md §Key invariants

**Context.** The obvious way to give users scriptable rules is a Lua callback invoked for every cell every generation. This is also catastrophic: a 1024² grid at 60 generations/second is 63 million interpreter calls per second, several orders of magnitude beyond what Lua can sustain, and it cannot run on the GPU at all.

**Options.**
- **A. Per-cell Lua callback.** Maximum expressiveness. Rejected on performance and on incompatibility with D-001.
- **B. Lua as a rule generator, executed once.** Chosen.
- **C. No scripting; DSL only.** Rejected: the whole point of F-008 is rules the DSL cannot express.

**Decision.** Option B. A Lua rule script runs exactly once, at rule-compile time, and returns an IR table. The interpreter is not linked into any per-cell path and must not be reachable from the step loop. The script may compute the transition table however it likes — including exhaustively, since it runs once.

**Consequences.**
- Scripts can be arbitrarily slow within their budget without affecting simulation throughput.
- Rules whose behaviour genuinely depends on per-cell runtime state that cannot be precomputed are not expressible. Accepted: no target automaton needs this.
- A sandbox and instruction budget are required, since a user script can loop forever at compile time (AV-009).
- This invariant is easy to erode later under pressure from a feature request. It is recorded as an architectural invariant precisely so that eroding it requires a superseding decision.

**Reversal conditions.** Revisit only if a compelling rule family emerges that cannot be precomputed into either backend form, and then only as an explicitly slow, explicitly CPU-only mode.

---

### D-004 Two backends, selected automatically from IR shape
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-009, D-002, AV-007, AV-010, SPEC.md §5, SPEC.md §6

**Context.** Most rules of interest are tiny as tables. A binary outer-totalistic 2D Moore rule is 18 entries; the 3D equivalent is 56; a non-totalistic 2D Moore rule is 512. But a non-totalistic 3D Moore rule is 2²⁶ entries per state, and continuous rules have no finite table at all. One backend cannot serve both ends of that range.

**Options.**
- **A. Lookup tables only.** Simplest, fastest for small rules. Rejected: excludes non-totalistic 3D and all continuous automata.
- **B. Generated GLSL only.** Uniform, handles everything. Rejected: incurs a shader compile on every rule change, which is unacceptable when rule mutation (F-015) changes the rule every few generations.
- **C. Both, with automatic selection on a size threshold.** Chosen.

**Decision.** Option C. The IR compiler emits a lookup table when the table would fit within the threshold in SPEC §5, and generated GLSL otherwise. Selection is automatic and not user-configurable; the chosen backend is displayed as metadata only. Rules expressible both ways must produce identical results.

**Consequences.**
- Rule mutation stays cheap for the common case, since mutating a lookup table is a texture update rather than a shader recompile.
- The codegen path needs a shader cache keyed on IR hash, or mutation on a large rule will stall on recompilation every interval.
- Two backends means two chances to disagree. This is the motivation for AV-007 and for the equivalence tests in Phase 4.
- The threshold is a tuning parameter with real consequences and belongs in SPEC, not scattered in code.

**Reversal conditions.** Revisit if the equivalence tests prove persistently hard to keep green, or if shader caching turns out to make the codegen path cheap enough that the table path stops earning its keep.

---

### D-005 Mutation as two independent controls at different pipeline stages
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-015, F-016, F-017, AV-011, AV-012, SPEC.md §9

**Context.** "Evolution and mutation" as originally described covers two different things: the rule itself drifting over time, and individual cells flipping randomly. They sound like one feature and are not — they act on different objects, at different stages, at different rates.

**Options.**
- **A. A single combined "chaos" control.** Simpler UI. Rejected: it conflates a rule-space walk with state-space noise, and users cannot then hold one fixed while varying the other, which is the main thing anyone would want to do.
- **B. Two independent controls.** Chosen.

**Decision.** Option B. Rule mutation operates on the IR, upstream of the compiler, every N generations. Cell mutation operates inside the compute step, downstream of the rule, every generation. Each draws from its own named RNG stream so that changing one does not perturb the sequence produced by the other.

**Consequences.**
- Rule mutation requires a recompile, so its interval interacts with backend choice (D-004).
- Cell mutation must be hashed from cell coordinate and generation index rather than drawn from a sequential stream, since GPU evaluation order is undefined.
- Two streams means two seeds in the session record (D-006).
- Rule mutation without a lineage log produces interesting rules and immediately loses them, which is why F-017 is a Must rather than a nicety.

**Reversal conditions.** Revisit if the two controls turn out never to be used independently in practice, which would suggest the combined control was right after all.

---

### D-006 Session reproducibility as a serialised quadruple
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-020, AV-006, SPEC.md §11

**Context.** With two stochastic sources active (D-005), a run that produces something interesting is unrecoverable unless the randomness is captured. Determinism cannot be bolted on afterwards: it constrains how RNG is drawn, where seeds live, and whether any wall-clock value may enter the step.

**Options.**
- **A. Snapshot the grid periodically.** Simple, robust, no determinism requirement. Rejected as the primary mechanism: snapshots of a 256³ grid are large, and a snapshot records the outcome without recording how it was reached, so the rule lineage is lost.
- **B. Record the generative parameters and replay.** Chosen.
- **C. Both.** Deferred. Snapshots may be added later as an optimisation for long replays, but the parameter record is the source of truth.

**Decision.** Option B. A session is initial state, rule IR, both RNG seeds, and the mutation schedule. Any run is replayable from this quadruple to a bit-identical grid at any generation. The format carries a version field from the first release.

**Consequences.**
- No wall-clock-derived or frame-rate-dependent value may enter the step loop. This is an architectural invariant, not a guideline.
- The CPU and GPU paths must agree bit-for-bit, since a session recorded on one must replay on the other (AV-007).
- Long replays cost time proportional to generation count, which is the trade accepted in rejecting option A.

**Reversal conditions.** Revisit if replay time for realistic sessions becomes a practical obstacle, at which point option C's periodic snapshots become worth the storage.

---

### D-007 C++20 with raylib and rlImGui
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley
**Related:** F-018, F-019, README.md §Build requirements

**Context.** The stack choice was effectively settled by precedent: Caustic and Clotho both use C++20 with raylib and rlImGui on Linux, and the same developer maintains all three.

**Options.**
- **A. C++20 + raylib + rlImGui.** Chosen — matches the existing generative-visuals projects, and raylib exposes compute shaders on GL 4.3, which D-001 requires.
- **B. Rust with wgpu.** Better shader tooling and a portability story across Vulkan and Metal. Rejected: no reuse from the sibling projects, and portability is not a requirement here.
- **C. Python with a GPU library.** Rejected: the UI and real-time loop are the wrong shape for it.

**Decision.** Option A.

**Consequences.**
- Shared idioms with Caustic and Clotho; patterns transfer in both directions.
- Compute shader support depends on raylib exposing the GL 4.3 path, which it does, but this is a dependency on a specific raylib capability rather than on raylib generally.
- No portability to platforms without desktop GL.

**Reversal conditions.** Revisit if raylib's compute shader support proves inadequate for the 3D path, or if cross-platform distribution becomes a goal.

---

### D-008 Dense uniform grid; no hashlife or sparse representation
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-001, F-004, D-001, FEATURES.md §Out of scope

**Context.** Hashlife achieves enormous speedups on Life by memoising repeated spacetime patterns, and a sparse representation saves memory on mostly-empty grids. Both are standard techniques in Life implementations and both were considered.

**Options.**
- **A. Hashlife or a similar memoising quadtree.** Rejected: it exploits determinism and pattern repetition, and cell mutation (F-016) destroys both. It is also specific to a narrow rule family and does not generalise to multi-state or continuous automata.
- **B. Sparse or chunked storage with active-region tracking.** Rejected for v1: it complicates the GPU path considerably for a benefit that only appears on sparse grids, and the automata of interest here tend towards dense activity. Recorded as a candidate optimisation rather than a rejection on principle.
- **C. Dense uniform grid in GPU textures.** Chosen.

**Decision.** Option C. Every cell is stored and stepped every generation regardless of activity.

**Consequences.**
- Cost is predictable and independent of pattern content, which makes performance budgets meaningful.
- Memory is bounded and calculable up front, which is what makes the VRAM guard in AV-001 possible.
- Large mostly-empty grids waste work. Accepted.
- Unbounded-universe automata are not supported; grids have fixed extent and a boundary condition.

**Reversal conditions.** Revisit chunked storage if a use case emerges for grids substantially larger than VRAM permits with activity confined to a small region.

---

### D-009 Project name: Aether
**Decided:** 2026-09-11
**Recorded:** 2026-08-30 (proposed); 2026-09-11 (accepted)
**Status:** Accepted
**Authors:** Claude (suggestion); Shane Hartley (adjudication)
**Related:** README.md

**Context.** Projects in this stable are named after Greek and Latin primordial deities (Nyx, Erebus, Phanes, Pontus, Talos, Apeiron, Mnemosyne, Caustic, Clotho). This project needs a name before the repository is created.

**Options.**
- **A. Aether.** The primordial upper air — the medium other things exist within. Fits a substrate on which patterns live. Chosen provisionally.
- **B. Physis.** Nature as the principle of growth and becoming. Fits the evolutionary aspect more directly but reads less like a substrate.
- **C. Thalassa.** Primordial sea. Evocative of continuous automata specifically, which are a late-phase feature; a poor fit for the discrete core.

**Decision.** Option A. Proposed 2026-08-30; confirmed by the author 2026-09-11 and the repository created under that name at `Darian-Frey/Aether`.

**Consequences.**
- Repository name, binary name, namespace and session file extension all follow from this and are cheap to change now, expensive after the first public commit.

**Reversal conditions.** The repository now exists, so the no-cost window has closed. Revisit only for a strong reason, and record the rename as a new entry superseding this one.

---

### D-010 IR accommodates continuous states from the outset
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-006, D-002, ROADMAP Phase 5, SPEC.md §4

**Context.** Continuous automata (SmoothLife, Lenia) are a Could-priority Phase 5 feature. The temptation is to design the IR purely for discrete states now and extend it later. Retrofitting float states into a structure built around integer state indices and finite transition tables is a rewrite, not an extension.

**Options.**
- **A. Discrete-only IR now, extend in Phase 5.** Rejected: the extension is a breaking change to the one structure every other module depends on, arriving after four phases of code has been written against it.
- **B. IR carries a cell-type field and an expression form from v1, with the continuous backend unimplemented until Phase 5.** Chosen.
- **C. Build the continuous path in Phase 1.** Rejected: it delays the discrete core for a Could-priority feature.

**Decision.** Option B. The IR includes a cell-type discriminant (`u8` or `f32`) and an expression-tree transition form alongside the table form from the first version. The continuous code paths in the backends are stubs that reject `f32` IRs with a clear diagnostic until Phase 5.

**Consequences.**
- A small amount of structure exists ahead of the feature that uses it, and stub rejection paths must be tested.
- The Phase 5 work becomes additive rather than a migration.
- The `kind` field in the IR does real work from the beginning, which keeps backend dispatch honest.

**Reversal conditions.** Revisit if F-006 is withdrawn entirely, at which point the discriminant becomes dead weight and can be removed.

---

### D-011 CPU reference implementation retained as a permanent correctness oracle
**Decided:** 2026-08-30
**Recorded:** 2026-08-30
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, design session 2026-08-30)
**Related:** F-002, AV-005, AV-007, ARCHITECTURE.md §Key invariants

**Context.** GPU compute is hard to debug and easy to get subtly wrong — a boundary condition off by one, a neighbourhood gathered in the wrong order, a mutation hash that differs from its CPU twin. Without a trusted reference there is nothing to compare against, and "it looks like Life" is not a correctness test.

**Options.**
- **A. GPU only; verify by eye and by known-pattern tests.** Rejected: known-pattern tests catch gross errors and miss exactly the subtle ones that matter, and cell mutation makes visual verification meaningless.
- **B. A CPU path written during bring-up and discarded.** Rejected: the divergences worth catching arrive later, when a backend or a shader changes.
- **C. A permanent CPU path, runtime-selectable, deliberately simple.** Chosen.

**Decision.** Option C. Every rule is executable on CPU. The CPU path is serial and unoptimised by design — it exists to be obviously correct, not fast. Equivalence between paths is an automated test, not a manual check.

**Consequences.**
- Every rule feature costs two implementations. Accepted as the price of a testable engine.
- Equivalence tests become the detection mechanism for several attack vectors at once (AV-005, AV-006, AV-007).
- Headless mode (F-022) makes these tests runnable without a display, which is why that feature is worth more than its Could priority suggests.

**Reversal conditions.** Revisit if maintaining the second path measurably slows feature work and the equivalence tests have gone a long stretch without catching anything.

---

### D-012 Hexagonal lattices in scope; triangular and Penrose remain candidates
**Decided:** 2026-09-11
**Recorded:** 2026-09-11
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, Phase 2 session 2026-09-11)
**Related:** F-023, D-008, FEATURES.md §Out of scope, SPEC.md §3

**Context.** FEATURES §Out of scope excluded all non-cubic lattices on the grounds that SPEC §3's neighbourhood model assumes an axis-aligned integer lattice. With the 2D core built, the cost of each lattice could be assessed against the engine as it actually is rather than as it was imagined.

**Options.**
- **A. Keep all non-cubic lattices out.** Rejected: a hexagonal lattice turns out to need nothing the engine does not already have.
- **B. Hexagonal only.** Chosen. In axial coordinates a hex lattice is the existing square lattice with a six-offset neighbourhood — the same fixed offset list for every cell — so `core/`, both steppers, the table layout, the LUT backend and the mutation machinery are untouched. The additions are a `NeighbourhoodType`, a DSL keyword, a rendering variant that maps pixels to axial coordinates, and the matching `View2D::cellAt` so painting agrees with pixels.
- **C. Hexagonal and triangular.** Deferred. Triangles alternate orientation, so the offset list depends on the parity of `x + y`: the lattice is no longer uniform, and both steppers and the table index would gain a parity branch. Bounded, but it bends the "one offset list for all cells" assumption that the equivalence tests rest on.
- **D. Everything including Penrose.** Rejected for now. An aperiodic tiling has no integer coordinates; cells become a graph with explicit adjacency, the step becomes a gather by index, rendering becomes a polygon list, and cell mutation would hash by cell index. That is a second engine in the sense D-008 already uses for agent-based automata.

**Decision.** Option B. Hexagonal lattices become F-023 (Should), delivered in Phase 2 after rule mutation, lineage and sessions so that the session format changes once. Triangular and Penrose lattices move from Out of scope to Candidate features, each with its cost stated.

**Consequences.**
- SPEC §3 gains a `hexagonal` neighbourhood type with its offset list and count formula `3r(r+1)`; the canonical order rule still applies.
- The IR's `neighbourhood.type` gains a value. This is additive; `ir_version` stays at 1.
- The hexagonal renderer is a second fragment shader sharing the palette pass; `View2D` gains a lattice-aware `cellAt`.
- The equivalence fixture set gains hexagonal rules.
- The Out of scope line in FEATURES is narrowed rather than deleted, per the append-only convention.

**Reversal conditions.** Revisit triangular if a rule family emerges that needs it and the parity branch proves cheap in practice. Revisit Penrose only as a deliberate graph-lattice engine with its own decision.

---

### D-013 Sessions record a journal of user actions; snapshots are conveniences
**Decided:** 2026-09-12
**Recorded:** 2026-09-12
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, Phase 2 session 2026-09-12)
**Related:** F-017, F-020, D-006, SPEC.md §11, AV-006

**Context.** D-006 makes a session the quadruple *initial state, rule, seeds, mutation schedule*. Implementing it showed that "mutation schedule" was doing more work than it looked: a laboratory run also has brush strokes, fills, clears, rule changes and parameter changes at arbitrary generations, and none of them is derivable from the seeds.

**Options.**
- **A. Forbid mid-run edits, or make each one restart the session at generation 0.** Rejected: painting into a running automaton is the point of the canvas, and restarting loses the lineage.
- **B. Snapshot the grid on every edit.** Rejected: a 256³ grid per brush stroke.
- **C. Journal every externally driven change with its generation, and replay the journal against the initial state.** Chosen. Rule mutations are not journaled — they regenerate from stream A — so the journal is small.

**Decision.** Option C. The session's reproducibility set is `grid`, `initial`, the two seeds and the journal. The current grid and stream A's state are stored as well, so a session resumes without replay; they are conveniences the replay test re-derives, which is D-006's deferred option C taken in its harmless form.

**Consequences.**
- Every `Simulation` mutator that a user can reach journals itself; `installRule` is the one route for rule changes, so a change cannot escape both the lineage and the journal.
- Replay is exact on either path, across processes; `aether replay` and `aether compare` make it a CTest.
- Grid rewind becomes time travel with truncation (BUG-006).
- Headless mode (F-022) arrives early in reduced form because the cross-process test needs it.

**Reversal conditions.** Revisit if journals grow large enough to dominate session files in practice — a long painting session could — at which point periodic snapshots plus journal-since-snapshot would be the next form.

---

### D-014 Cell ageing as a front-end desugaring, not a cell-model change
**Decided:** 2026-09-14
**Recorded:** 2026-09-14
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-14)
**Related:** F-025, F-018, D-002, IMP-001, SPEC.md §7, SPEC.md §13

**Context.** A run should be able to give its cells a life cycle: a cell the rule stops supporting should fade over several generations rather than vanish, and should be coloured as it ages, so that what survives is what keeps producing new cells. Generations rules (`B/S/C`) already do this for the `B/S` notation alone; nothing offers it to a table-block rule, a hexagonal rule or a multi-state rule.

**Options.**
- **A. A hard lifespan: every cell dies at age L whatever its neighbours do.** Considered and set aside by the author in favour of B. It is a sharper dynamic — still lifes and oscillators die, only patterns that keep producing new cells persist — but it is a different automaton, not an ageing tail, and it cannot be reached from the existing `/C` semantics.
- **B. Soft decay: a cell the rule sends to `0` from a non-zero state instead enters an ageing tail and advances through it.** Chosen. It is the Generations semantics generalised to every rule the DSL can write.
- **C. A per-cell age field beside the state.** Rejected. It doubles grid memory, and changes the cell model (SPEC §1), the grid (§2), the IR (§4), both steppers, the shader, the session format and the palette — to express what the state index already expresses. Its one advantage is that age would be invisible to neighbours; with age-as-state a rule can see how old its neighbours are, which is a capability rather than a cost.
- **D. Age-as-state, desugared in the front end.** Chosen as the implementation of B: `decay N;` appends N states and rewrites the table, producing an ordinary `outer_totalistic` IR.

**Decision.** Options B and D. `decay N;` is a front-end transform in `rule/decay`, IR in and IR out. Tail states count as quiescent for the rule's own conditions, matching `/C`. `metadata.decay_from` records where the tail starts, as a presentation hint for palettes and age shading; it is excluded from `ir_hash` and carries no semantics.

**Consequences.**
- Nothing downstream changes: both backends, both execution paths, both mutation controls, the lineage, the session format and the equivalence tests are untouched. This is invariant 1 (the IR is the only compile target) paying for itself.
- The state count grows with the tail, so the table grows combinatorially. Until the codegen backend exists, the longest tail is 6 states on 2D Moore r=1, 8 on hexagonal r=1 and 14 on 2D von Neumann r=1; the compiler names the limit when it refuses. This makes IMP-001 the enabling work for long tails rather than an optimisation.
- `/C` and `decay` now express the same idea by two routes. A test asserts they produce the same table; IMP-003 proposes unifying them.
- A hard lifespan (option A) remains available later as a second modifier; it needs no engine change either.

**Reversal conditions.** Revisit option C only if a rule family emerges that needs age to be invisible to neighbours, which none of the target automata do.

---

### D-015 Cell mutation groups by aligned blocks
**Decided:** 2026-09-14
**Recorded:** 2026-09-14
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-14)
**Related:** F-016, F-026, D-005, D-006, SPEC.md §9.2, SPEC.md §11

**Context.** Cell mutation is per-cell independent, so it reads as uniform speckle. A run is more interesting if noise sometimes arrives as a patch — a group of cells disturbed at once — while staying reproducible.

**Options.**
- **A. Aligned blocks: every cell in a `2^k` block shares the decision to mutate.** Chosen. One parameter, and the block hash at `k = 0` is the cell hash, so the original behaviour is the `k = 0` case exactly and existing sessions replay unchanged.
- **B. Discs at a hashed centre.** Also stateless and deterministic, and more organic to look at, but it needs a centre, a radius and a count per generation — three parameters where blocks need one. Kept as a possible second form.
- **C. Structural groups: a connected cluster of live cells mutates as a unit.** Rejected for this engine. It needs connected-component labelling every generation, which is not a function of a cell's neighbourhood and cannot be done in one invocation per cell; it would need multi-pass label propagation and would break the step model. Recorded as a candidate feature with that cost stated.

**Decision.** Option A. `CellMutation` gains a block shift `k`. The *decision* comes from the block's hash and the *replacement state* from the cell's own hash, so a mutating block is a burst of noise rather than one flat colour. `p` keeps its meaning at every `k`: the expected fraction of cells changed per generation is `p`, clumped rather than scattered.

**Consequences.**
- Stream B stays stateless and order-independent, so both execution paths agree and replay is unaffected (SPEC §10). The equivalence suite runs with `k > 0`.
- `mutation.cell.block` joins the session record, defaulting to 0 so earlier files load and replay identically. It was added inside `format_version` 1 because nothing has been released against that version.
- The GLSL and C++ hashes gain a twin function each; they are tested against each other like the rest.

**Reversal conditions.** Revisit if blocks prove too obviously grid-aligned in use, at which point option B's discs are the natural second form.

---

### D-016 A counted-set kind, so that a rule pays for what it asks
**Decided:** 2026-09-14
**Recorded:** 2026-09-14
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-14)
**Related:** IMP-001, IMP-003, F-025, F-010, D-004, SPEC.md §4, SPEC.md §5

**Context.** `outer_totalistic` indexes on the whole vector of neighbour-state counts, so its table is `S·C(N+S−1, S−1)` — combinatorial in the state count. Almost no rule needs that. Generations rules, cyclic rules, Wireworld and any rule with an ageing tail ask a single question: how many neighbours are in *one* set of states. Paying the combinatorial price for a question never asked had become the binding constraint on three separate features: an ageing tail was capped at six states, a many-state generations rule fell to a backend that does not exist, and the fourteen-state cyclic rule could not be bundled at all.

**Options.**
- **A. Leave it; the codegen backend will run these rules.** Rejected: codegen exists for rules with no finite table, not as an escape from a bad table layout, and it costs a shader compile per rule where a table costs an upload — which matters under rule mutation (D-004).
- **B. A counted *state*: index on `(own, count of one designated state)`.** The form IMP-001 first proposed. Rejected as too narrow: an ageing tail counts *live* neighbours, which is a set, and `n(0)` is the complement of one.
- **C. A counted *set*, chosen per own state.** Chosen. `index = own·(N+1) + |neighbours ∈ counted[own]|`, so the table is `S·(N+1)`. Making the set depend on the own state costs eight words per state and is what lets a cyclic rule — where each state counts its own successor — use the form at all.

**Decision.** Option C, as a new kind `counted_totalistic` rather than a flag on `outer_totalistic`, so that a rule's kind continues to determine its indexing scheme exactly as SPEC §5 says. The counted sets are part of the rule: they are validated, hashed, serialised and mutated with it.

The front ends choose the form, since nothing downstream can infer it:
- The DSL detects it. For each own state it collects the states its conditions ask about; if that is at most one — with `n(0)` read as "everything that is not 0" — the rule is counted. It is used only when it is strictly smaller, so every binary rule keeps the IR and the hash it had.
- A Lua script declares `counted` as a list or a function of the own state, because its transition is opaque.
- `decay` propagates the base rule's sets and gives the tail an empty one, which is exactly the semantics wanted: a fading cell is counted by nobody.

**Consequences.**
- An ageing tail is no longer capped by the table: `decay` runs to the 256-state limit of SPEC §1 on every lattice. Sixty states cost 558 entries.
- `B/S/C` is now the two-state rule plus a tail through the one decay transform (IMP-003), so Generations rules of any length compile to a table. `B2/S/C25` was an expression no backend could run and is 225 entries.
- The fourteen-state cyclic rule joins the library at 126 entries, from 2.8 million.
- Rules that genuinely need several counts keep the full vector, unchanged.
- The hash of a rule with no counted sets is untouched, so sessions written before this change still load and replay; `ir_version` stays at 1 because nothing about an older IR has changed meaning.
- Both execution paths gain an index scheme, which is two more chances to disagree — the equivalence fixtures now include counted rules with a set that varies per own state, which is the case a mistake would show up in.

**Reversal conditions.** Revisit if a third indexing scheme is ever wanted, at which point the kinds are doing enough work to deserve a table of index functions rather than a switch.

---

### D-017 Patterns carry extended RLE where it reaches and a native format where it does not
**Decided:** 2026-09-15
**Recorded:** 2026-09-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-15)
**Related:** F-012, F-027, F-028, F-010, AV-016, SPEC.md §11, README.md

**Context.** F-012 asked for "standard Life RLE files" and stopped there. RLE as the Life community uses it describes a two-state pattern on a 2D square lattice. This engine runs up to 256 states (SPEC §1) on square, hexagonal and 3D lattices, and most of the fourteen bundled rules are outside the family RLE can describe. A library of creatures to drop into a grid therefore cannot be written in the format F-012 named: a Wireworld diode, a cyclic-CA seed and a 3D Bays glider are all unrepresentable in it.

**Options.**
- **A. Standard RLE only, as F-012 is written.** Rejected: it serves Life and little else. The bundled library could hold spaceships and guns and nothing from the other nine rules, and export would have to refuse most of what is on screen at any given moment.
- **B. One native format for everything.** Rejected, though it is the smaller job — the session cell codec already encodes a grid of any lattice and state count, so this is nearly free. It cuts the project off from sixty years of published Life patterns, every one of which would have to be retyped by hand to enter the library.
- **C. Extended RLE where it reaches, a native format where it does not.** Chosen. Golly's extended RLE carries multi-state 2D patterns through its state letters and names its rule in the header, so Life-like, Generations, cyclic and Wireworld patterns move in and out in a format other tools already read. Hexagonal and 3D patterns, which no RLE dialect describes, use a native JSON format sharing the session's cell codec. The reader picks by extension; the writer picks by what the pattern is.
- **D. Extend RLE ourselves to cover hexagonal and 3D.** Rejected. A private dialect of a format other tools read is worse than a plainly separate format: the file would claim to be RLE, fail to open in Golly, and the failure would look like a defect in whichever tool the user blamed first. A distinct extension is honest about what it is.

**Decision.** Option C. F-012 widens from import to import and export and names both formats. F-027 adds the bundled pattern library that `patterns/` has been reserved for since the tree was created. F-028 adds region seeding, which is the same gesture — put something into part of the grid — arriving from the random side rather than the library side. All three sit in Phase 6, where F-012 already was; nothing here blocks Phase 5.

**Consequences.**
- SPEC gains §14, the pattern format: the extended-RLE subset accepted and emitted, the native JSON schema, and the rule by which a pattern whose lattice or state count does not match the grid is refused rather than coerced.
- Placement is a grid mutation, so it journals itself like every other user-reachable mutator and a session that pastes a creature replays it. Without that the feature would quietly break the determinism contract of D-006.
- A pattern file is external data reaching the engine, which is what `rule/ir_json` is for rules: validate, then build, never build while validating. AV-016 records the failure mode.
- `patterns/` gains content and a search order matching `rules/` — `$AETHER_PATTERNS`, `./patterns`, `<exe>/patterns`, `<exe>/../patterns`.
- README's "RLE pattern library" line narrows to what the directory actually holds.
- A pattern that came in as RLE and has not left what RLE can say goes back out as RLE. Where the grid has drifted past that, export writes the native format and says which it chose.

**Reversal conditions.** If the extended-RLE reader proves larger than the corpus it unlocks is worth, fall back to option B — the native format alone, with a one-way RLE importer. The native format is what the library depends on; RLE is the bridge to everyone else's work.

---

### D-018 The pattern editor is a host-side scratch pad, and its inspector is the oracle itself
**Decided:** 2026-09-15
**Recorded:** 2026-09-15
**Status:** Accepted
**Authors:** Shane Hartley (with Claude, session 2026-09-15)
**Related:** F-029, F-030, F-012, F-027, IMP-005, AV-017, AV-002, D-011

**Context.** Authoring a creature cell by cell needs two things the engine does not have: somewhere to draw that is not the running simulation, and a way to see what the rule will do to a cell and its neighbours. The second is the harder half, though not for the reason it first appears. The information is not new or expensive — `cpuStep` computes every part of it for every cell of every generation — but it is computed inside a loop body and discarded, with no per-cell entry point to ask for it.

**Options — where the editing happens.**
- **A. In place on the live grid.** Rejected for now. It shows the creature in the context it will live in, which is genuinely the better view, but every edit journals, experimenting means altering the run being watched, and on the GPU path the host grid is stale — so an inspector open on a live grid needs a readback every frame, which is AV-002 in the place AV-002 warns about.
- **B. A host-side scratch pad.** Chosen. A small grid of its own, stepped on the CPU path, independent of whatever the simulation is doing. No readback, no journal entries, no disturbance to a running session, and being small it can afford luxuries the main grid cannot — a history ring giving a step-backward control, for one.
- **C. Both.** Deferred, not rejected. The inspector built for B works unchanged on a live grid; what C adds is the region readback and a policy for how often to take it. Left as a reversal condition rather than a candidate feature, because the decision that would need revisiting is this one.

**Options — where the explanation comes from.**
- **D. The inspector derives the transition itself.** Rejected. It is the obvious implementation and it is a trap: a second implementation of SPEC §5's index arithmetic and SPEC §6's evaluation rules, free to drift from the first, whose entire purpose is to be believed. This is the AV-005 and AV-007 failure mode in a new place, and worse there than in a backend, because a divergent backend produces visibly odd automata while a divergent inspector produces confident prose. It is consulted precisely when the user cannot check the answer.
- **E. The inspector calls the oracle.** Chosen. `cpuStep`'s loop body becomes a function that returns a cell's next state together with the working that produced it; the stepper is a loop over that function and the inspector is a single call to it. One implementation, and the equivalence suite covers it from the moment it is extracted.

**Decision.** B and E. F-029 is the scratch pad, F-030 the inspector, IMP-005 the extraction that makes E possible. All three are Phase 6, with F-012 and F-027 — an editor with no format to save into is half a feature.

**Consequences.**
- `sim/cpu_step` gains a per-cell entry point returning the transition and its working: neighbour states as gathered, the counts or table index derived, the entry or clause that fired, the resulting state. `cpuStep` becomes a loop over it, so there remains exactly one implementation of what a cell does. IMP-005 records the refactor and the allocation trap in it — invariant 8 applies to the CPU path too.
- AV-017 records the divergence this is all guarding against, with the test that catches it: the inspector's predicted next state must equal what the stepper writes, for every cell of every equivalence fixture under every boundary. Edges and corners are the cells that matter, since an inspector resolving neighbours differently from `sim::resolve` explains the interior perfectly and lies about the rim.
- The scratch pad is host-side, so no readback is needed and `core/gpu_grid` gains nothing. Option C would need a `downloadRegion`, and on GL 4.3 that is a compute shader copying a region into an SSBO — `glGetTextureSubImage` is 4.5 and not available to us.
- The scratch pad sits outside the session and the journal by design: it is not part of the run, so it neither replays nor perturbs replay. Placing its contents into the live grid does journal, through F-012's placement path.
- It defaults to the live simulation's rule, so what it shows is what the creature will do where it is going, with any bundled rule selectable instead.
- Being host-side and free of GL, the scratch pad and the inspector are testable with no display, which most of `ui/` is not.
- Its contents are a pattern as SPEC §14 defines one. No new format.

**Reversal conditions.** If the inspector turns out to be wanted more on a running grid than on the scratch pad, promote option C. The inspector itself does not change — it is already the oracle — and the work is the region readback plus a policy for when to take it.
