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
