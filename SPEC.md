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

For `d=3` von Neumann, `N = (2r+1)(2r²+2r+3)/3 − 1`; at `r=1, 2, 3` this is 6, 24, 62. (Corrected 2026-09-11, BUG-001.)

Concrete counts in use: 2D Moore r=1 gives `N=8`; 3D Moore r=1 gives `N=26`; 3D von Neumann r=1 gives `N=6`.

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
              "neighbourhood" ("moore"|"von_neumann") integer ";"
              [ "boundary" ("wrap"|"zero"|"mirror") ";" ]
statement  := integer ":" condition "->" integer ";"
condition  := count_expr | signature_literal
count_expr := "n" "(" integer ")" comparison integer
              { ("and"|"or") count_expr }
comparison := "==" | "!=" | "<" | "<=" | ">" | ">="
```

Example — Conway's Life written longhand:
```
states 2;
neighbourhood moore 1;
0: n(1) == 3 -> 1;
1: n(1) < 2 -> 0;
1: n(1) > 3 -> 0;
```
Statements are evaluated in order; the first match wins. Cells matching no statement retain their state. The compiler expands the statement list exhaustively into a `Table`, or into an `Expression` if the table would exceed the threshold.

Notes fixed by the Phase 1 implementation (2026-09-11): `and` binds tighter than `or`; `n(0)` counts quiescent neighbours and is derived as `N − Σ n(s≠0)`; `#` introduces a comment to end of line; `B`, `S` and `C` are accepted in either case. `signature_literal` is named in the grammar but not yet defined or accepted — a table block for a non-totalistic rule is deferred until a literal syntax is specified. Generations rules whose table would exceed the threshold (see IMP-001) are lowered to an `Expression` by the same route as an oversized table block.

---

## 8. Lua front-end contract

A rule script is a Lua chunk returning a table convertible to a `RuleIR`. It executes exactly once per compile (D-003).

**Sandbox.** Available: `math`, `string`, `table`, `ipairs`, `pairs`, `select`, `tonumber`, `tostring`, `type`, `error`, `assert`. Removed: `io`, `os`, `require`, `dofile`, `loadfile`, `load`, `package`, `debug`, and the global environment beyond the above.

**Budget.** A debug hook aborts the script after `LUA_INSTRUCTION_BUDGET = 50_000_000` VM instructions, reported as a compile error naming the budget. Wall-clock is not used, so the budget is deterministic (AV-009).

**Returned table.** Field names mirror the IR. A returned table failing IR validation (§4) is a compile error, reported with the failing rule number.

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
h = hash32(x, y, z, generation, seed_B)
if (h < p · 2³²) next = uniform_state(h)
else            next = rule_output
```

The hash is a function of coordinate and generation only, never of evaluation order or thread index, so the result is independent of how the GPU schedules work and reproduces exactly on the CPU path. At `p = 0` the comparison is false everywhere and the branch is uniform across the wavefront, so cost is negligible.

`uniform_state(h)` derives a state in `0 … S-1` from the upper bits of `h` by multiply-shift, not modulo, to avoid bias when `S` is not a power of two.

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

The hash used by stream B is specified once and implemented twice — in C++ for the reference path and in GLSL for the compute path — and the two implementations must produce identical output for identical input. This is a test, not a hope (AV-007).

**Prohibited everywhere in `sim/` and in shaders:** `rand()`, `std::random_device`, time-derived seeds, thread-index-derived randomness, and any RNG not drawn from stream A or B.

---

## 11. Session format

Extension `.aether`. A JSON document, optionally accompanied by a binary sidecar for large initial grids.

```
{
  "format_version": 1,
  "grid":      { "dimensions": 2, "w": 1024, "h": 1024, "d": 1,
                 "cell_type": "u8", "boundary": "wrap" },
  "initial":   { "encoding": "rle" | "raw" | "random",
                 "data": "...",            // inline for rle, sidecar path for raw
                 "density": { "1": 0.35 }  // for random
               },
  "rule":      { "ir": { ... }, "ir_hash": "0x...",
                 "source_notation": "B3/S23" },
  "rng":       { "seed_a": 12345, "seed_b": 67890 },
  "mutation":  { "rule": { "interval": 250, "magnitude": 1, "enabled": true },
                 "cell": { "p": 0.0001, "enabled": true } },
  "lineage":   [ { "generation": 0, "ir_hash": "0x...", "pinned": false }, ... ],
  "generation": 4210
}
```

**Determinism contract.** Given `initial`, `rule`, `rng` and `mutation`, replaying from generation 0 reproduces the grid at any generation bit-for-bit, on either execution path, on any machine meeting the build requirements. `generation` and `lineage` are conveniences, derivable from the other four; they are stored so that a loaded session can display its history without replaying.

`format_version` is checked on load. An unknown version is an error, not a best-effort parse.

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
| Cell mutation at `p = 0` | < 2% throughput cost vs disabled | F-016 |
| VRAM, 256³ `u8` grid pair | ≤ 40 MB | AV-001 |

These are acceptance thresholds, not aspirations. Baselines go in `BENCHMARKS.md` when it is created in Phase 6.

---

## 13. Rendering

**2D.** The state texture is sampled directly by a fragment shader and mapped through a 256-entry palette texture. Pan and zoom are a transform on texture coordinates; at 1:1 zoom the mapping is pixel-exact with nearest sampling. Optional age shading darkens by state index for generations rules.

**3D.** Front-to-back raymarch through the 3D state texture with per-state colour and opacity from the same palette, plus an alpha multiplier per state so that quiescent cells can be made fully transparent. Step count adapts to grid extent. Adjustable clipping planes and a single-slice mode for inspection. Instanced cube rendering is a candidate alternative for small grids and is not specified here.

**1D.** The current generation is written into a scrolling history texture, one row per generation, displayed as a space-time diagram. History depth is the window height in rows; older generations are discarded, not stored.

**Palette.** 256 RGBA entries, editable, saved with the rule rather than the session so that a rule carries its intended appearance into the library.
