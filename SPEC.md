# Spec

Authoritative technical reference for Aether. Where this document and the code disagree, the code is wrong until a DECISIONS entry says otherwise.

Sections marked *(Phase 5)* are specified now but not implemented until that phase; see D-010.

---

## 1. Cell and state model

A cell holds one value. Two cell types exist, discriminated by the IR's `cell_type` field:

| Type | Storage | Range | Phase |
|---|---|---|---|
| `u8` | one unsigned byte | `0 … S-1` where `S` is the rule's state count | 1 |
| `f32` | one 32-bit float | `[0.0, 1.0]` | 5 |

`S` is bounded: `2 ≤ S ≤ 256`. State `0` is conventionally the quiescent state and is what boundary conditions and grid clears produce; nothing in the engine enforces this, but rule libraries and palettes assume it.

For `u8`, state values are opaque indices. The engine attaches no meaning to them beyond `0`; interpretation (alive/dead, age, wire/head/tail) belongs to the rule and the palette.

---

## 2. Grid model

The grid is a dense uniform lattice of fixed extent. Every cell is stored and stepped every generation (D-008).

**Dimensions.** `d ∈ {1, 2, 3}`. Extents `W`, `H`, `D`; unused extents are 1.

**Storage.** Two textures of identical format, the ping-pong pair. One is read, one is written; they swap after each generation.

| `d` | Texture target | Internal format (`u8`) | Internal format (`f32`) |
|---|---|---|---|
| 1 | `GL_TEXTURE_2D` (see below) | `GL_R8UI` | `GL_R32F` |
| 2 | `GL_TEXTURE_2D` | `GL_R8UI` | `GL_R32F` |
| 3 | `GL_TEXTURE_3D` | `GL_R8UI` | `GL_R32F` |

The 1D case stores the current generation as a single row and maintains a separate history texture for the space-time diagram; the history texture is presentation state and is not part of the simulation.

**Memory footprint.** `bytes = W · H · D · sizeof(cell) · 2` for the pair. At `u8`, a 256³ grid is 33.5 MB; 512³ is 268 MB. The renderer's palette and history buffers are small by comparison. The VRAM guard (AV-001) checks this figure plus a 25% headroom allowance before allocating.

**Boundary conditions.** Selected per session, applied identically on both execution paths:

| Value | Behaviour |
|---|---|
| `wrap` | Toroidal. Coordinates taken modulo extent. Default. |
| `zero` | Out-of-bounds neighbours read as state `0` (or `0.0`). |
| `mirror` | Out-of-bounds coordinates reflect about the edge cell's centre: `−1 → 1`, `W → W−2`, folding repeatedly for coordinates further out; an extent of 1 always maps to 0. No cell is ever its own neighbour. (Clarified 2026-09-11, BUG-003.) |

Boundary handling is part of rule semantics, not a rendering detail. A CPU/GPU disagreement here is AV-005.

---

## 3. Neighbourhoods

A neighbourhood is a type and a radius `r ≥ 1`. The cell itself is never a member.

| Type | `d=1` | `d=2` | `d=3` | Count formula |
|---|---|---|---|---|
| `moore` | 2r | (2r+1)²−1 | (2r+1)³−1 | `(2r+1)^d − 1` |
| `von_neumann` | 2r | 2r(r+1) | see below | Manhattan distance ≤ r |
| `hexagonal` | 2r | 3r(r+1) | — | axial hex distance `max(|dq|, |dr|, |dq+dr|) ≤ r` (added 2026-09-12, D-012) |

For `d=3` von Neumann, `N = (2r+1)(2r²+2r+3)/3 − 1`; at `r=1, 2, 3` this is 6, 24, 62. (Corrected 2026-09-11, BUG-001.)

Concrete counts in use: 2D Moore r=1 gives `N=8`; 3D Moore r=1 gives `N=26`; 3D von Neumann r=1 gives `N=6`; hexagonal r=1 gives `N=6`, r=2 gives 18.

**Hexagonal lattices** (D-012, F-023) reuse the square storage: cell `(x, y)` is the axial coordinate `(q, r)` of a pointy-topped hex lattice, and the neighbourhood is the fixed offset set above, so every execution path and the table layout are unchanged. A `W × H` grid is therefore a rhombus of hexes on screen rather than a rectangle, and `wrap` is a rhombic torus. Offset ("odd-r") coordinates would give a rectangle but need parity-dependent neighbourhoods, which is the triangular-lattice problem D-012 declined. Hexagonal neighbourhoods are rejected in 3D.

**Neighbour ordering.** For non-totalistic rules the neighbour order is part of the rule's meaning and must be identical on both execution paths. The canonical order is lexicographic by offset `(dz, dy, dx)` ascending, skipping `(0,0,0)`. Bit `i` of a neighbourhood signature corresponds to the `i`-th neighbour in this order.

---

## 4. Rule IR

The IR is the sole compile target (D-002). It is a validated data structure; nothing outside `rule/` constructs one directly.

```
RuleIR {
  ir_version    : u16              // bumped on any breaking schema change
  dimensions    : 1 | 2 | 3
  cell_type     : "u8" | "f32"
  states        : u16              // 2..256; ignored when cell_type == f32
  neighbourhood : { type, radius }
  boundary      : "wrap" | "zero" | "mirror"
  kind          : Kind
  transition    : Table | Expression | Kernel
  metadata      : { name?, author?, source_notation? }
}
```

**Kind** determines both the transition form and the lookup-table indexing scheme:

| `kind` | Transition depends on | Form |
|---|---|---|
| `outer_totalistic` | own state + count of each neighbour state | `Table` |
| `totalistic` | sum over cell and neighbours | `Table` |
| `non_totalistic` | own state + ordered neighbour signature | `Table` or `Expression` |
| `expression` | arbitrary function of own state and neighbours | `Expression` |
| `continuous` *(Phase 5)* | convolution result and growth function | `Kernel` |

**Table** is a flat array of state indices, sized and indexed per §5.

**Expression** is a small tree over: the own-state value, indexed neighbour values, neighbour-state counts, integer and float literals, arithmetic (`+ - * / %`), comparison, boolean connectives, and a conditional. Deliberately restricted: no loops with data-dependent bounds, no function calls, no recursion. Every expression must be translatable to branch-free or statically-bounded GLSL (D-001).

**Kernel** *(Phase 5)* is a convolution kernel — either a radial profile sampled to a matrix, or an explicit matrix — plus a growth function expressed as an `Expression` over the convolution result.

### Validation

An IR is valid only if all of the following hold. Validation runs on every IR regardless of origin, including IRs produced by rule mutation (AV-012).

1. `2 ≤ states ≤ 256` when `cell_type == u8`.
2. Every state index appearing in a `Table` or as an `Expression` literal result is in `0 … states-1`.
3. `Table` length matches exactly the size computed in §5 for the rule's kind, dimensionality and neighbourhood.
4. `radius ≥ 1`, and the resulting `N` does not exceed 64 for `non_totalistic` kinds (the signature must fit a `u64`).
5. `cell_type == f32` implies `kind == continuous`, and conversely.
6. The expression tree contains no unbound references and has a type-consistent root.

### IR hash

`ir_hash` is a 64-bit hash over the canonical serialisation of every field except `metadata`. It identifies a rule for the shader cache, the lineage log, and the session record. Two IRs with the same hash must behave identically; the converse is not required.

---

## 5. Lookup-table layout

Tables are stored in a shader storage buffer, one entry per `uint`, and indexed by an integer computed in the shader. (Originally specified as a 1D `GL_R8UI` texture; changed 2026-09-11, BUG-004, because `GL_MAX_TEXTURE_SIZE` for 1D textures is 32768 on NVIDIA, below `LUT_MAX_ENTRIES`.)

**Outer-totalistic.** The signature is the vector of neighbour-state counts. For binary rules this collapses to a single count `k ∈ 0…N`:

```
index = own_state · (N + 1) + k
size  = states · (N + 1)
```

For `states > 2`, the signature is the count vector `(c₁ … c_{S−1})` over states `1 … S-1` (the count of state 0 is implied by `N − Σcᵢ`). The vectors with `Σcᵢ ≤ N` are ranked densely in lexicographic order, so that

```
index = own_state · W(N, S−1) + rank(c₁ … c_{S−1})
size  = states · W(N, S−1)              where W(n, m) = C(n+m, m)
```

`W(n, m)` counts vectors of `m` non-negative integers summing to at most `n`. The rank of a vector is the sum, over each digit `cᵢ`, of `W(R − v, S−1−i−1)` for every `v < cᵢ`, where `R` is the budget remaining after the preceding digits. For `S = 2` this collapses to the binary form above. Both execution paths implement this ranking; the `W` table is precomputed on the host and uploaded alongside the transition table. (Clarified 2026-09-11, BUG-002.)

**Totalistic.** As above but with `own_state` folded into the sum:

```
index = sum(own_state, neighbours)
size  = (N + 1) · (states - 1) + 1
```

**Non-totalistic.** The signature is the ordered neighbour vector in base `S`:

```
signature = Σ neighbour[i] · S^i         (i in canonical order, §3)
index     = own_state · S^N + signature
size      = states · states^N
```

### Backend threshold

```
LUT_MAX_ENTRIES = 65536
```

A rule compiles to the table backend if its computed table size is `≤ LUT_MAX_ENTRIES`, and to the GLSL codegen backend otherwise (D-004). Worked examples:

| Rule | Size | Backend |
|---|---|---|
| Binary 2D Moore r=1 outer-totalistic (`B3/S23`) | 2·9 = 18 | table |
| 4-state 2D Moore r=1 outer-totalistic | 4·W(8,3) = 4·165 = 660 | table |
| Binary 2D Moore r=1 non-totalistic | 2·2⁸ = 512 | table |
| Binary 3D Moore r=1 outer-totalistic | 2·27 = 54 | table |
| Binary 3D Moore r=1 non-totalistic | 2·2²⁶ ≈ 1.3×10⁸ | codegen |
| Any `continuous` | — | codegen |

The size computation must happen before allocation, never as a consequence of it (AV-010).

---

## 6. GLSL codegen contract

The codegen backend substitutes a generated function body into a fixed template. The generated function has the signature:

```glsl
uint aether_rule(uint self, uint nbr[N]);        // u8 cell type
float aether_rule_f(float self, float conv);     // f32 cell type (Phase 5)
```

Requirements on generated code:

1. No loops with data-dependent bounds. Loops over the fixed neighbourhood are unrolled or statically bounded by `N`.
2. No side effects, no global writes, no texture access. Neighbours arrive as parameters.
3. Deterministic across drivers: no `fma` reassociation assumptions, no reliance on undefined-precision built-ins.
4. Integer arithmetic only for `u8` rules. Float appears only in the `f32` path.

Compiled shaders are cached keyed on `ir_hash`. A cache hit skips compilation entirely, which is what makes rule mutation viable on codegen-backed rules (D-004).

---

## 7. Rule DSL grammar

Three notations, tried in order. The parser reports line and column on failure and leaves the previously compiled rule active.

**Life-like (B/S).**
```
rule    := "B" digits "/" "S" digits
digits  := [0-9]*
```
`B3/S23` is Conway's Life. Implies `states=2`, `kind=outer_totalistic`, Moore r=1, dimensionality from the session.

**Generations (B/S/C).**
```
rule := "B" digits "/" "S" digits "/" "C" integer
```
`B2/S/C3` is Brian's Brain. `C` sets the state count; states `2 … C-1` are refractory and advance unconditionally toward `0`.

**Table block.** For anything the shorthand cannot express:
```
rule_block := header statement*
header     := "states" integer ";"
              "neighbourhood" ("moore"|"von_neumann"|"hex"|"hexagonal") integer ";"
              [ "boundary" ("wrap"|"zero"|"mirror") ";" ]
              [ "decay" integer ";" ]
statement  := integer ":" condition "->" integer ";"
condition  := term { ("and"|"or") term }
term       := count_expr | signature_literal
count_expr := "n" "(" integer ")" comparison integer
signature_literal := "[" element { "," element } "]" [ "rot" ]
element    := integer | "_"
comparison := "==" | "!=" | "<" | "<=" | ">" | ">="
```

**Signature literals** *(defined 2026-09-14, IMP-002)*. A literal lists the neighbour states in the canonical order of §3, one element per neighbour, and matches when every element agrees with the cell's neighbourhood. `_` matches any state. A rule using one is `non_totalistic`, so its table is `states · states^N` and the §5 threshold binds much sooner than it does for count conditions; a block whose table would exceed it is refused rather than lowered, since an expression backend does not exist yet.

`rot` expands a literal to the rotations of its pattern, which is how transition tables for rotation-symmetric automata are published. One rotation is a quarter turn on a square lattice and a sixth of a turn on a hexagonal one; both map the neighbourhood onto itself at every radius. Rotation is defined for 2D lattices only and is refused elsewhere rather than guessed at.

A condition may mix literals and count conditions with `and` and `or`, since both are predicates on the same neighbourhood.

```
# A cell with a live neighbour directly above it and nothing to its left.
states 2;
neighbourhood von_neumann 1;
0: [1, 0, _, _] -> 1;
```

For 2D von Neumann r=1 the canonical order is `[above, left, right, below]`; for 2D Moore r=1 it is the three cells above in left-to-right order, then left and right, then the three below.

Example — Conway's Life written longhand:
```
states 2;
neighbourhood moore 1;
0: n(1) == 3 -> 1;
1: n(1) < 2 -> 0;
1: n(1) > 3 -> 0;
```
Statements are evaluated in order; the first match wins. Cells matching no statement retain their state. The compiler expands the statement list exhaustively into a `Table`, or into an `Expression` if the table would exceed the threshold.

**Decay** *(added 2026-09-14, F-025, D-014)*. `decay N;` gives the rule an ageing tail: `N` states are appended to the rule's own states, and a cell the rule would send to `0` from a non-zero state instead enters the tail and advances through it one state per generation before reaching `0`. Tail states are counted as state `0` by the rule's own conditions, so a decaying cell neither feeds births nor supports survival. This is the Generations semantics of `/C` generalised to any table-block rule: `B2/S/C3` and

```
states 2;
neighbourhood moore 1;
decay 1;
0: n(1) == 2 -> 1;
```

compile to the same table. `C` and `decay` are related by `C = states + N`.

The desugaring is a front-end transform: it produces an ordinary `outer_totalistic` IR with `states + N` states, so every backend, both execution paths, both mutation controls, the lineage and the session format are unchanged (D-014). Two limits follow from that:

- `states + N ≤ 256` (SPEC §1).
- The resulting table must fit `LUT_MAX_ENTRIES` until the codegen backend exists. Since an outer-totalistic table is `S·C(N_nb+S−1, S−1)`, the largest tail for a binary rule is 6 states on 2D Moore r=1, 8 on hexagonal r=1, and 14 on 2D von Neumann r=1. The compiler rejects a longer tail and names the largest that fits.

`metadata.decay_from` records the first tail state so that palettes and age shading can colour the tail as a ramp (§13). It is a presentation hint, excluded from `ir_hash` like the rest of `metadata`, and carries no semantics: a wrong value gives odd colours, never a different automaton.

Notes fixed by the Phase 1 implementation (2026-09-11): `and` binds tighter than `or`; `n(0)` counts quiescent neighbours and is derived as `N − Σ n(s≠0)`; `#` introduces a comment to end of line; `B`, `S` and `C` are accepted in either case. (`signature_literal` was named in the grammar but undefined until 2026-09-14; it is specified above.) Generations rules whose table would exceed the threshold (see IMP-001) are lowered to an `Expression` by the same route as an oversized table block.

---

## 8. Lua front-end contract

A rule script is a Lua chunk returning a table convertible to a `RuleIR`. It executes exactly once per compile (D-003).

**Sandbox.** Available: `math`, `string`, `table`, `ipairs`, `pairs`, `select`, `tonumber`, `tostring`, `type`, `error`, `assert`. Removed: `io`, `os`, `require`, `dofile`, `loadfile`, `load`, `package`, `debug`, and the global environment beyond the above.

**Budget.** A debug hook aborts the script after `LUA_INSTRUCTION_BUDGET = 50_000_000` VM instructions, reported as a compile error naming the budget. Wall-clock is not used, so the budget is deterministic (AV-009). A second budget bounds memory: the interpreter runs on an allocator capped at `LUA_MEMORY_BUDGET = 256 MB`, since a script can exhaust memory well inside the instruction budget by building a table rather than by looping (added 2026-09-14). Exceeding either is a compile error naming the budget, and leaves the running rule alone like any other failed compile.

**Isolation.** The interpreter is created and destroyed inside one compile call, and the chunk runs with a fresh environment table as its `_ENV`, so the real global table is unreachable even by name. Nothing Lua-owned outlives the call, which is what keeps D-003 and AV-008 structural rather than a matter of discipline: there is no interpreter for a step loop to call into.

**Returned table.** Field names mirror the IR: `dimensions`, `states`, `neighbourhood = {type, radius}`, `boundary`, `kind`, `transition`, and an optional `metadata`. A returned table failing IR validation (§4) is a compile error quoting the diagnostic.

`transition` takes one of two forms (2026-09-14):

- **An array of state indices**, in the layout order of §5, whose length must equal the computed table size exactly. This mirrors the IR as stored.
- **A function**, which the host calls once per table entry while building it — at compile time, like everything else here, so D-003 is untouched. Its arguments follow the rule's kind:
  - `outer_totalistic`: `f(own, counts)` where `counts[s]` is the number of neighbours in state `s`, including `counts[0]`.
  - `non_totalistic`: `f(own, neighbours)` where `neighbours[i]` is the `i`-th neighbour in the canonical order of §3, one-based.
  - `totalistic`: `f(sum)`, the sum over the cell and its neighbours.

  The function must return a state in `0 … S-1`; anything else is a compile error naming the arguments that produced it.

The function form is what makes a large hand-specified automaton practical to write, since the script can compute an entry rather than lay out a table of thousands. `expression` and `continuous` rules cannot yet be returned: no backend can execute one.

---

## 9. Mutation semantics

Two independent mechanisms at different pipeline stages (D-005), each with its own RNG stream (§10).

### 9.1 Rule mutation

Parameters: `interval` (generations, ≥ 1) and `magnitude` (point edits per event, ≥ 1).

Every `interval` generations, before the step:

1. Draw `magnitude` point edits from stream **A**.
2. Apply them to a copy of the current IR.
3. Validate the mutated IR (§4). An invalid result is discarded and redrawn, up to 8 attempts; after 8 failures the mutation event is skipped and logged.
4. Recompile. On a cache hit this is free; otherwise it is a table upload or a shader compile.
5. Append to the lineage log (§9.3) with the generation index at which the new rule takes effect.

**Point edit definition by transition form:**

| Form | Point edit |
|---|---|
| `Table` | Select a uniformly random table index; replace its value with a uniformly random state in `0 … S-1`, excluding the current value. |
| `Expression` | Select a uniformly random node; if a literal, perturb by ±1 (clamped to the valid range for its position); if a comparison or connective, replace with another operator of the same arity. Tree shape is never altered. |
| `Kernel` *(Phase 5)* | Perturb one kernel coefficient or one growth-function literal by a Gaussian of width `0.05`, clamped to range. |

Tree shape is held fixed for `Expression` deliberately: shape mutation would produce mostly-invalid trees and turn the redraw loop into the common path.

### 9.2 Cell mutation

Parameter: `p ∈ [0, 1]`, probability per cell per generation.

Evaluated inside the compute step, after the rule has produced the next state:

```
h_cell  = hash32(x, y, z, generation, seed_B)
h_block = hash32(x >> k, y >> k, z >> k, generation, seed_B)
if (h_block < threshold(p)) next = uniform_state(mix32(h_cell ^ 0xA5A5A5A5))
else                        next = rule_output
```

The hash is a function of coordinate and generation only, never of evaluation order or thread index, so the result is independent of how the GPU schedules work and reproduces exactly on the CPU path. `generation` is the index of the generation being read. At `p = 0` the threshold is 0, the comparison is false everywhere and the branch is uniform across the wavefront, so cost is negligible (measured: none, 2026-09-11).

**Block size** *(added 2026-09-14, F-026, D-015)*. `k` is the block shift: cells are grouped into aligned blocks of `2^k` per axis, and every cell in a block shares the decision to mutate while drawing its own replacement state. `k = 0` is one cell per block, where `h_block` and `h_cell` are the same value and the behaviour is identical to the original per-cell form — so a session recorded before this existed replays unchanged. `p` keeps its meaning at any `k`: a block mutates with probability `p` and every cell in it changes, so the expected fraction of cells changed per generation is `p` regardless of grouping. What changes is that the changes arrive in clumps rather than as uniform speckle.

`threshold(p)` is `⌊p · 2³²⌋` clamped to `0 … 2³²−1`, so `p = 1` selects every hash but `0xFFFFFFFF`. `uniform_state(v)` derives a state in `0 … S-1` as `(v · S) >> 32` — multiply-shift, not modulo, to avoid bias when `S` is not a power of two. The state is derived from a *second* mixing of `h`, not from `h` itself: a hash that passed the test is small by construction, so its upper bits would select state 0 almost always (BUG-005, corrected 2026-09-11).

### 9.3 Lineage log

An append-only list of entries:

```
LineageEntry {
  generation : u64          // generation at which this rule took effect
  ir_hash    : u64
  ir         : RuleIR       // stored in full for the initial entry and any pinned entry;
                            // stored as a delta from the previous entry otherwise
  pinned     : bool
  name       : string?      // set when pinned
}
```

Entry `0` is the session's initial rule. Operations: **pin** (mark, name, and save to the rule library) and **rewind** (restore that rule as current, optionally restoring the grid by replaying from generation 0 to that entry's generation).

The log is part of the session record and survives save and load. Rule mutation that does not append to the lineage log is an incomplete operation (ARCHITECTURE §Key invariants).

---

## 10. Random number generation

Two named streams, seeded independently, recorded in the session (§11):

| Stream | Used by | Draw method |
|---|---|---|
| **A** | rule mutation, random grid fill | Sequential PCG32, CPU only |
| **B** | cell mutation | Stateless hash of `(x, y, z, generation, seed_B)` |

Stream A is sequential because its consumers are ordered and CPU-side. Stream B is stateless because its consumers are massively parallel with undefined ordering.

The hash used by stream B is specified once and implemented twice — in C++ (`sim/hash.hpp`) for the reference path and in GLSL (`shaders/hash.glsl`) for the compute path — and the two implementations must produce identical output for identical input. This is a test, not a hope (AV-007). The definition, all in 32-bit unsigned arithmetic with wraparound:

```
mix32(v):  v ^= v >> 16;  v *= 0x7FEB352D;  v ^= v >> 15;  v *= 0x846CA68B;  v ^= v >> 16
hash32(x, y, z, generation, seed_B):
    h = seed_lo ^ 0x9E3779B9
    h = mix32(h ^ x);  h = mix32(h ^ y);  h = mix32(h ^ z)
    h = mix32(h ^ gen_lo);  h = mix32(h ^ gen_hi);  h = mix32(h ^ seed_hi)
```

where `gen_lo/hi` and `seed_lo/hi` are the low and high 32 bits of the 64-bit `generation` and `seed_B`. `mix32` is the `lowbias32` finaliser. (Fixed 2026-09-11.)

**Prohibited everywhere in `sim/` and in shaders:** `rand()`, `std::random_device`, time-derived seeds, thread-index-derived randomness, and any RNG not drawn from stream A or B.

---

## 11. Session format

Extension `.aether`. A JSON document, accompanied by a raw sidecar `<file>.grid` for grids over 4M cells (2026-09-12: the sidecar holds the initial cells followed by the current cells).

```
{
  "format_version": 1,
  "grid":      { "dimensions": 2, "w": 1024, "h": 1024, "d": 1,
                 "cell_type": "u8", "boundary": "wrap" },
  "initial":   { "encoding": "rle" | "raw", "data": "..." },   // cells at generation 0, before any event
  "rule":      { "ir": { ... }, "ir_hash": "0x...", "source_notation": "B3/S23" },   // the current rule
  "rng":       { "seed_a": 12345, "seed_b": 67890,
                 "stream_a_state": ["0x...", "0x..."] },       // convenience: stream A at `generation`
  "mutation":  { "rule": { "interval": 250, "magnitude": 1, "enabled": true },
                 "cell": { "p": 0.0001, "block": 0, "enabled": true } },   // current parameters
  "journal":   [ { "generation": 0, "type": "fill", "density": [0.3] },
                 { "generation": 0, "type": "rule_mutation", "enabled": true, "interval": 250, "magnitude": 1 },
                 { "generation": 812, "type": "paint", "x0": 3, "x1": 20, "y": 7, "z": 0, "state": 1 },
                 { "generation": 1500, "type": "set_rule", "ir": { ... } },
                 ... ],
  "lineage":   [ { "generation": 0, "ir_hash": "0x...", "origin": "initial", "journal_index": 0,
                   "pinned": false, "ir": { ... } },
                 { "generation": 250, "ir_hash": "0x...", "origin": "mutation", "journal_index": 3,
                   "pinned": false, "delta": [[17, 1]], "metadata": { "name": "B3/S23*" } },
                 ... ],
  "generation": 4210,
  "state":     { "encoding": "rle", "data": "..." },           // convenience: cells at `generation`
  "counters":  { "rule_mutations": 16, "rule_mutations_skipped": 0 }
}
```

**The journal** (2026-09-12) is the record of every externally driven change, stamped with the generation at which it happened: `set_rule`, `rewind` (rule-only), `paint` (one row span), `fill` (draws from stream A), `clear`, `cell_mutation` (a change of `p`) and `rule_mutation` (a change of the parameters). Rule mutations themselves are *not* journaled; they regenerate from stream A. This is the "mutation schedule" of D-006 made concrete: without it, a brush stroke at generation 700 would make the run irreproducible.

**Replay.** Starting from `initial` with stream A seeded by `seed_a`, apply every journal event with generation `g` before the step from `g` to `g+1`, in journal order; rule mutation runs at the top of each step as §9.1 says. This reproduces the grid at any generation bit-for-bit on either execution path.

**Determinism contract.** Given `grid`, `initial`, `rng.seed_a`, `rng.seed_b` and `journal`, replaying from generation 0 reproduces the grid at any generation bit-for-bit, on either execution path, on any machine meeting the build requirements. `rule`, `mutation`, `lineage`, `generation`, `state`, `stream_a_state` and `counters` are conveniences derivable from those five; they are stored so that a session resumes instantly and displays its history without replaying. `aether replay` re-derives them and `aether compare` checks them; the CTest `replay.*` cases do exactly this across processes.

**Lineage entries** carry `origin` (`initial` | `user` | `mutation` | `rewind`) and `journal_index`, the journal length when the entry was made. The initial and pinned entries store the full IR; other table-form entries store a `delta` of `[index, value]` pairs against the previous entry plus their `metadata`. Every entry stores its `ir_hash` and the loader verifies it after reconstruction.

**Cell encoding.** `rle` is byte run-length pairs `(count ≤ 255, value)`, base64. `raw` names the sidecar.

`mutation.cell.block` is the block shift of §9.2 and defaults to `0` when absent, so files written before it existed load and replay identically. It was added within `format_version` 1 rather than bumping the version because nothing has been released against version 1; a field whose default changes behaviour would need a bump (2026-09-14).

`format_version` is checked on load. An unknown version is an error, not a best-effort parse.

**Grid rewind** (F-017) to lineage entry *i* is a replay to `journal_index` and the entry's generation (running the mutation due there if the entry is one); the journal and lineage are then truncated to that point, since the run's future from there is abandoned. The rule-only rewind keeps everything and appends. (Clarified 2026-09-12, BUG-006.)

---

## 12. Performance budgets

Measured on the target machine (ThinkPad P15 Gen 2i, NVIDIA T1200 4 GB).

| Configuration | Target | Feature |
|---|---|---|
| 2D binary, 1024², LUT backend | ≥ 200 generations/second | F-003 |
| 2D binary, 1024², CPU reference | ≥ 5 generations/second | F-002 |
| 3D binary, 256³, LUT backend | ≥ 30 generations/second | F-004 |
| 3D volume render, 256³ | ≥ 30 fps | F-019 |
| Rule mutation event, table backend | < 1 ms | F-015 |
| Rule mutation event, codegen backend, cache miss | < 250 ms | F-015 |
| Cell mutation at `p = 0` | < 2% throughput cost vs disabled (measured none, 2026-09-11; `p > 0` costs ~10%) | F-016 |
| VRAM, 256³ `u8` grid pair | ≤ 40 MB | AV-001 |

These are acceptance thresholds, not aspirations. Baselines go in `BENCHMARKS.md` when it is created in Phase 6.

---

## 13. Rendering

**2D.** The state texture is sampled directly by a fragment shader and mapped through a 256-entry palette texture. Pan and zoom are a transform on texture coordinates; at 1:1 zoom the mapping is pixel-exact with nearest sampling. Optional age shading darkens by state index for generations rules.

**3D.** Front-to-back raymarch through the 3D state texture with per-state colour and opacity from the same palette, plus an alpha multiplier per state so that quiescent cells can be made fully transparent. Step count adapts to grid extent. Adjustable clipping planes and a single-slice mode for inspection. Instanced cube rendering is a candidate alternative for small grids and is not specified here.

**1D.** The current generation is written into a scrolling history texture, one row per generation, displayed as a space-time diagram. History depth is the window height in rows; older generations are discarded, not stored.

**Age colouring** *(2026-09-14)*. A rule with an ageing tail (§7 `decay`) carries `metadata.decay_from`, the first tail state. The default palette then ramps the tail from the colour of state 1 toward the background, with alpha falling to zero at the oldest state, so a cell visibly fades as it ages — in 2D through the colour ramp, in 3D through both colour and opacity. The age-shading toggle darkens only the tail when `decay_from` is known, rather than darkening by raw state index, which is meaningless for a rule whose states are not ages (Wireworld, cyclic).

**Palette.** 256 RGBA entries, editable, saved with the rule rather than the session so that a rule carries its intended appearance into the library.
