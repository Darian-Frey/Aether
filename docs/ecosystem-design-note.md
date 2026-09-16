# Aether — Ecosystem Extensions Design Note

> **Status:** Proposal
> **Provenance:** Design discussion, 2026-09-16
> **Last reviewed:** 2026-09-16
> **Why this status:** Feature ideas agreed in principle; none implemented yet. The provisional IDs below are the note's own and do **not** correspond to the project registers, where those numbers are already taken — read them as labels local to this document.
>
> **Entered into the registers 2026-09-16 by D-019**, which accepted the gather-compatible part of this note and refused the rest. What landed: F-031 multi-field grids (§2.1, as parallel fields rather than a cell record), F-032 the abiotic resource field and sessile resource-feeders (§8.1, §8.2 in part), F-033 per-cell genomes with inheritance (§2.2, §3, bounded to what a shader can interpret), F-034 hard cell lifespan (§4), F-035 similarity-biased birth (§5.1), F-036 population and field readouts (§10), and AV-018 for the conservation problem this note calls C-001. What did not: §5.2 swap movement, §6 clan energy sharing and §7 feeding as an energy transfer, all three being writes from one cell to another where the engine's step writes only its own cell. D-019 records the reasoning and the two routes back. This document is kept as written, as the provenance the decision cites.

## 1. Purpose

Aether currently models classical cellular automata: one global rule, stateless binary (or small finite-state) cells, synchronous update. This note describes a set of extensions that turn the grid into an evolving ecosystem — genomes, life cycles, movement, cooperation, predation, and a resource/plant layer — while keeping classical automata available as the degenerate case.

## 2. Core architectural shift

Every feature below requires cells to carry per-cell data. This is the one non-negotiable change; everything else layers on top of it.

### 2.1 Cell record (D-001, provisional)

Replace the scalar cell state with a struct along the lines of:

```
Cell {
    state    : u8 / float      // classical CA state; float for Lenia-style continuous rules
    genome   : Genome           // the cell's own rule (see 2.2)
    age      : u32              // steps since birth
    energy   : f32              // stored energy; death at <= 0
    clan     : u32              // lineage/organism tag for cooperation
    role     : u8               // differentiated role within a clan (0 = undifferentiated)
}
```

Empty sites are a sentinel (genome = none). The step function reads neighbours' full records, not just `state`.

Classical mode is recovered by giving every cell the same genome, ignoring `age`/`energy`, and disabling mutation — so existing rule files continue to work unchanged.

### 2.2 Genome

The genome *is* the rule, stored per cell:

- For Life-like rules: the B/S bitmask (18 bits for Moore; larger for extended neighbourhoods).
- For generalised rules: a bitstring/lookup table, or a kernel + growth-function parameter vector for continuous rules.
- Plus trait genes used by the ecosystem features: `mobile` (bool), `toxicity`/`hardness` (f32), `metabolism` (f32), `fertility_window` (age range), and so on.

Provide `genome_hash()` for colouring and `hamming(a, b)` / `sim(a, b)` for similarity queries.

### 2.3 Update model (D-002, provisional)

Keep synchronous update as the default. Features that need movement (herding, predation) introduce an optional **swap phase** after the rule phase; see 4.3 for conflict resolution. Document clearly that enabling movement makes the model an agent-based lattice model rather than a strict CA.

### 2.4 Energy conservation (C-001, provisional)

Energy enters the system **only** via the abiotic resource layer / photosynthesis, and leaves **only** via the per-step metabolic leak and death. No other code path may create energy. Evolution will find and exploit any leak within minutes, producing infinite-energy blobs. Add an assertion or debug counter that tracks total system energy against inputs and losses.

## 3. Mutation and inheritance (F-001)

Aether already plans two mutation controls: rule-space search over time, and stochastic per-cell state flipping. This feature adds a third: **genome inheritance with mutation at birth**.

- On birth, the child's genome is derived from its live neighbours by one of:
  - **majority vote** per gene/bit across parents;
  - **random parent** pick;
  - **crossover** (uniform or single-point) between two parents.
- Apply a per-gene mutation probability `p_mut` after inheritance.
- **Group vs single-cell mode:** single-cell mode mutates individual births independently; group mode applies the same mutation to a whole clan at once (models a lineage-level change).

Outcome: competing rule lineages on one grid. Colour by `genome_hash()` to visualise spread and extinction.

## 4. Life cycle (F-002)

- Increment `age` each step for live cells.
- Rules may read `age`. Suggested defaults, all genome-tunable:
  - juvenile period with reduced death sensitivity;
  - fertility window `[age_min, age_max]` outside which the cell cannot act as a parent;
  - hard senescence: death at `age_max_life` regardless of neighbours.

Even in plain Life this destabilises still-lifes and oscillators, so nothing persists without reproduction.

## 5. Herding (F-003)

Two options, ordered by preference:

**5.1 Birth-bias (stays a strict CA).** When several empty sites are candidates for birth, weight each by the genetic similarity of its neighbouring live cells to the parents. No movement; clusters of similar genomes emerge from where births land.

**5.2 Swap movement (agent-based).** After the rule phase, each `mobile` cell may swap with an adjacent empty site in the direction of maximum mean `sim()` to its neighbours. Requires conflict resolution when two cells want the same site — resolve by random tie-break, by higher energy, or by a fixed scan order (document which). Movement costs energy (see 8).

Implement 5.1 first; 5.2 becomes cheap once the swap phase exists for predation.

## 6. Cooperation / multicellular organisms (F-004)

- `clan` tag propagates on birth (child takes the parent clan; a rare mutation spawns a new clan).
- **Differentiation:** a cell's `role` is chosen by a small rule over its same-clan neighbour count and position (e.g. edge cells with fewer clan neighbours become `edge`, interior cells become `core`, cells adjacent to non-clan cells become `sensor`).
- Each role may run a distinct sub-rule from the genome. Energy may be shared within a clan (bounded transfer per step) so specialised cells that cannot feed are sustained by those that can.

Prior art: Lenia and Flow Lenia organisms, and morphogenesis models.

## 7. Predator–prey (F-005)

- Every live cell pays a metabolic cost per step (`metabolism` gene).
- A `mobile` cell adjacent to a cell of a different clan may **feed**: transfer a fraction `f_take` of the target's energy to itself. If the target's energy hits 0 it dies.
- Feeding on a plant is grazing (see 8.2); feeding on a mobile cell is predation. A `diet` gene (herbivore / carnivore / omnivore) gates which targets are valid.
- Reproduction requires `energy >= repro_threshold`; the child takes a share of the parent's energy.
- Mutation (F-001) now acts under real selection pressure.

Prior art: Wa-Tor, Avida, Tierra.

## 8. Resource layer (F-006) — two-fold

### 8.1 Abiotic resources

- A separate scalar grid (`resource[x][y]`) rather than random point placement. Seed it as a noise field (Perlin or a few Gaussian blobs) so it is patchy — uniform resources produce uniform populations and nothing interesting happens.
- Each step: regenerate toward a per-site carrying capacity at rate `r_regen`; optionally diffuse with coefficient `D`.
- `r_regen` is the primary "harshness of the world" knob.

### 8.2 Plants

Plants are not a separate system: they are cells whose genome has `mobile = false` and a photosynthesis rate.

- **Growth:** Life-like birth into adjacent empty sites, gated on local `resource` availability (light/minerals). Growth consumes resource.
- **Storage:** plants accumulate energy each step proportional to local resource; this stored energy is what grazers take.
- **Evolution:** plant genomes mutate like everything else, so strategies emerge — fast spreaders that burn energy quickly vs slow, dense, high-energy plants.
- **Defence gene** (`toxicity` / `hardness`): costs energy to maintain, reduces `f_take` for grazers lacking a matching `tolerance` gene. This creates a plant–grazer arms race with no extra machinery.

### 8.3 Trophic chain

`resource → plant → grazer → predator`, with energy leaking at each step so the pyramid stays a pyramid.

### 8.4 Known failure mode and damping (C-002, provisional)

Three-level systems oscillate hard: plants boom, grazers boom and strip the grid, both crash, grid dies. Provide damping options:

- a minimum plant seed rate from the abiotic layer (spores / seed bank);
- refugia — sites mobile cells cannot enter;
- a soft cap on `f_take` so grazing is partial by default.

Expose these in the UI; a dead grid is a valid outcome but should be an opt-in one.

## 9. DSL and scripting exposure

The rule DSL should expose the new fields so most ecosystem rules are writable without dropping to the scripting layer:

- Cell fields: `state`, `age`, `energy`, `clan`, `role`, `genome.<gene>`
- Neighbourhood queries: `count(state == alive)`, `count(clan == self.clan)`, `sim(n)` (genetic similarity to neighbour `n`), `nearest(diet_match)`
- Environment: `resource(here)`, `resource(n)`
- Actions: `feed(n, fraction)`, `move_toward(predicate)`, `share_energy(n, amount)`, `differentiate(role)`

The scripting layer keeps full access for exotic rules.

## 10. Visualisation

- Three colour channels: resource (background), plant (green-ish channel), mobile cells (third channel), with genome-hash tinting within each.
- Per-species / per-clan population graph over time — needed to see the oscillations in 8.4.
- Total-energy readout for verifying C-001.

## 11. Suggested implementation order

1. Cell record and genome storage; classical mode regression test (D-001, 2.1–2.2).
2. Energy field and metabolic leak with conservation counter (C-001).
3. Life cycle (F-002) — smallest visible payoff, exercises per-cell fields.
4. Mutation with inheritance (F-001).
5. Abiotic resource grid (8.1).
6. Plants (8.2) — feeding from resource only.
7. Grazing and predation (F-005) plus the swap phase (D-002).
8. Herding via birth-bias (5.1), then swap-based herding (5.2).
9. Clans and differentiation (F-004).
10. Damping controls and population graphs (8.4, 10).

## 12. Open questions

- Genome representation for continuous (Lenia-style) rules — parameter vector size and mutation operator.
- Whether `clan` energy sharing should be adjacency-limited or organism-wide.
- 3D: all of the above generalises, but the swap phase and visualisation need separate thought.
- Whether plants should occupy the same cell layer as mobile cells (one occupant per site) or a separate layer allowing a mobile cell to sit "on" a plant.
