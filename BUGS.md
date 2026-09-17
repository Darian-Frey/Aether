# Bugs

Catalogue of bugs discovered during development. Per Maintenance Rule 8, bugs are logged here when found, not silently fixed. The author decides whether to fix immediately, defer, or leave alone.

This is the dual of [ATTACK_VECTORS.md](ATTACK_VECTORS.md): attack vectors are anticipated failure modes with detection methods; bugs are realised failures with status flags. A recurring bug pattern may warrant a new AV entry; an AV entry that escaped detection becomes a bug.

Status vocabulary: open | fixed | wontfix | deferred.
Severity vocabulary: low | medium | high.

Entries are kept in ID order within each section. Entry format:

```markdown
### BUG-NNN: {short title}
**Status:** open
**Found:** YYYY-MM-DD ({session/commit context})
**Fixed:** YYYY-MM-DD ({once it is})
**Location:** {path/to/file.ext:line, or "cross-cutting"}
**Severity:** {low | medium | high}
**Description.** {What's wrong and why it matters.}
**Reproduction.** {Minimum steps to trigger.}
**Notes.** {Related context, suggested fix, links to BUG/IMP/D/AV entries. Written while the bug is open, and left as written.}
**Resolution (YYYY-MM-DD, commit `hash`).** {What was actually done, once it is fixed, and any call the fix made that the author may want to revisit.}
```

## Open

### BUG-011: a continuous rule goes wrong at 512², differently on each driver
**Status:** open
**Found:** 2026-09-17 (Phase 5 step 5, running the bundled Lenia rule at the size SPEC §12 asks for)
**Location:** `shaders/continuous_step.comp`, `src/sim/gpu_step.cpp`; not reproduced on the CPU path
**Severity:** high
**Description.** The bundled `lenia.lua` is stable at 128² and 256²: the CPU path holds 28% mass from generation 250 to at least 5000, and both GPUs agree with it bitwise. At 512² both GPUs go wrong, and not in the same way.

- **NVIDIA (T1200)** stays alive at the right mass but grows four cells of `0xFFFFFFFF` — a quiet NaN, all bits set — at coordinates (0,0), (1,0), (2,0) and (3,0), plus one wildly out-of-range value at (8,0). They appear between generation 4000 and 5000, at the same coordinates for every seed tried. A value the arithmetic cannot produce: the growth function is multiplies, subtractions and a select over finite inputs, with no division since 2026-09-17, and the step ends in a `clamp` to [0, 1].
- **NVIDIA is also not deterministic with itself.** The same binary, the same seed, two runs of 5000 generations: 5 of 1,048,576 bytes differ. That is a straight breach of D-006, and it is the finding that matters most here.
- **Intel (Mesa)** does the opposite: the grid is empty by generation 1000, where NVIDIA and the CPU path both hold 28%.

**Reproduction.**
```
aether headless --lua rules/lenia.lua --size 512x512 --generations 5000 --seed 3 --save a.aether
aether headless --lua rules/lenia.lua --size 512x512 --generations 5000 --seed 3 --save b.aether
aether compare a.aether b.aether          # differs on NVIDIA, identical on Intel
```
At `--size 256x256 --generations 200` the same rule is identical across the CPU path, Mesa and NVIDIA, so the size is the trigger rather than the rule.
**Notes.** Not reproduced below 512²; 256² is clean under every combination tried, so it is not simply grid size in the arithmetic sense — 256 and 512 are both exact multiples of the 8×8 local size. The suspects in order: a barrier that is sufficient for a small dispatch and not a large one (`step()` issues `GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT` between generations); the `glGetTexImage` download racing the last dispatch; and something specific to `r32f` image load/store that the `r8ui` path never exercised, since the discrete path has never been compared run-to-run at 1024². The coordinates being the first cells of row 0 — the start of the buffer — points at the transfer rather than at the automaton.

This does not affect the discrete path, which has its own equivalence coverage, and does not affect continuous rules at the sizes the test suite exercises. It does mean Phase 5's acceptance (`512² without state divergence over 10,000 generations`) is **not met**, and that the determinism contract does not currently hold for `f32` on NVIDIA at that size.

## Fixed

### BUG-001: SPEC §3 closed form for 3D von Neumann neighbour count is wrong
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `rule/neighbourhood` tests)
**Fixed:** 2026-09-11
**Location:** SPEC.md §3
**Severity:** low
**Description.** SPEC gave `N = 2r(2r²+3r+2)/3` for 3D von Neumann, which evaluates to 4.67 at r=1 while the same paragraph states the r=1 answer is 6. Enumeration gives 6, 24, 62 for r=1,2,3. The correct closed form is `(2r+1)(2r²+2r+3)/3 − 1`. Documentation only; no code was written against the wrong formula.
**Reproduction.** Evaluate the old formula at r=1.
**Notes.** Fixed in SPEC the same day. The enumeration in `neighbourOffsets()` is the source of truth and the test checks the closed form against it, which is how this was caught.

### BUG-002: SPEC §5 multi-state outer-totalistic index encoding did not match its own size formula
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `rule/table_layout`)
**Fixed:** 2026-09-11
**Location:** SPEC.md §5
**Severity:** medium
**Description.** For `states > 2` SPEC described the count vector as "a mixed-radix integer over counts of states 1…S-1" but gave the table size as the number of compositions (worked example: 4 states, N=8 → ≈660). A mixed-radix encoding with radix N+1 per digit needs `S·(N+1)^(S−1)` entries (2916 for that example), so the two statements were inconsistent, and the difference decides backend selection for every multi-state rule. Resolved in favour of the size formula, which is the more specific statement: count vectors are ranked densely in lexicographic order, giving exactly `S·C(N+S−1, S−1)` entries. `TableLayout::indexOuterTotalistic` implements the ranking and the GLSL side must implement the same one.
**Reproduction.** Compare the two sentences in the old §5 "Outer-totalistic" paragraph for S=4, N=8.
**Notes.** SPEC §5 reworded to state the ranking explicitly. This is a clarification of an ambiguous paragraph, not a change to `LUT_MAX_ENTRIES` or to the selection rule.

### BUG-003: SPEC §2 `mirror` boundary did not say which reflection
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `sim/boundary`)
**Fixed:** 2026-09-11
**Location:** SPEC.md §2
**Severity:** medium
**Description.** "Reflect about the boundary" admits two readings: reflection about the grid edge (`−1 → 0`, which is what `GL_MIRRORED_REPEAT` does) and reflection about the edge cell's centre (`−1 → 1`). They produce different automata — under the first, a corner cell counts itself up to three times among its Moore neighbours. Both execution paths must pick the same one (AV-005), so the spec has to say which.
**Reproduction.** Place an L of three live cells in a corner under B3/S23 with `mirror`; the two readings give a block and a three-cell result respectively.
**Notes.** Resolved as reflection about the edge cell's centre, since a cell being its own neighbour has no counterpart in any automaton this project targets. `sim::resolve()` is the reference; the GLSL side must not use sampler address modes for this. The corner-L case is now a test.

### BUG-004: SPEC §5 table storage as a 1D texture cannot hold a threshold-sized table
**Status:** fixed
**Found:** 2026-09-11 (Phase 1, writing `sim/gpu_step`)
**Fixed:** 2026-09-11
**Location:** SPEC.md §5
**Severity:** medium
**Description.** SPEC specified the lookup table as a 1D `GL_R8UI` texture. The NVIDIA driver reports `GL_MAX_TEXTURE_SIZE = 32768`, which bounds 1D textures too, while `LUT_MAX_ENTRIES` is 65536. A rule anywhere in the upper half of the table range would have failed to allocate on the target machine. Changed to a shader storage buffer, which has no such bound and is what the compute path already uses for its other inputs.
**Reproduction.** `glTexStorage1D(GL_TEXTURE_1D, 1, GL_R8UI, 65536)` on the T1200.
**Notes.** SPEC §5 reworded. No semantic change: the table contents and the index arithmetic are identical; only the container differs.

### BUG-005: SPEC §9.2 derived the mutated state from the hash that had just passed the threshold test
**Status:** fixed
**Found:** 2026-09-11 (Phase 2, implementing stream B)
**Fixed:** 2026-09-11
**Location:** SPEC.md §9.2
**Severity:** high
**Description.** The pseudocode tested `h < p·2³²` and then took the replacement state from "the upper bits of `h`" by multiply-shift. A hash that passed the test is numerically small, so its upper bits are near zero and `(h·S) >> 32` is 0 for any realistic `p`. Cell mutation as specified would have set every mutated cell to state 0 — a decay process, not the uniform replacement the feature describes. Would have been invisible in equivalence tests, since both paths would have agreed on the wrong thing.
**Reproduction.** With `p = 0.001`, `h < 4.3×10⁶`, so `(h·S) >> 32 = 0` for all `S ≤ 256`.
**Notes.** The state now comes from `uniform_state(mix32(h ^ 0xA5A5A5A5))`. `tests/sim/hash_test.cpp` checks that replacement states among cells that passed a 0.1% threshold are spread across the range.

### BUG-006: SPEC §9.3 called the lineage append-only and also offered a grid rewind
**Status:** fixed
**Found:** 2026-09-12 (Phase 2, implementing sessions)
**Fixed:** 2026-09-12
**Location:** SPEC.md §9.3, §11
**Severity:** medium
**Description.** Rewinding the grid to an earlier entry means replaying to that point and continuing from there; the run's original future (later journal events, later mutations) no longer describes the run. Keeping those entries in an append-only list would make later mutations regenerate identically and appear twice, and a replayed journal would re-apply abandoned brush strokes at their old generations. The two requirements conflict.
**Reproduction.** Rewind the grid to entry 3 of a 10-entry run and step: with an untruncated lineage, entry 4's rule reappears as entry 11.
**Notes.** Resolved: grid rewind truncates journal and lineage to the target point (time travel); rule-only rewind appends and keeps everything. SPEC §11 states this.

### BUG-007: GpuStepper's move constructor dropped fields added after it was written
**Status:** fixed
**Found:** 2026-09-12 (Phase 2, the session replay test)
**Fixed:** 2026-09-12
**Location:** `src/sim/gpu_step.cpp`
**Severity:** high
**Description.** `Simulation::create` returns by value, so its `GpuStepper` is moved. The hand-written move constructor listed members by name and never learned about `mutation_` or the five uniform locations added later. In a moved stepper the locations were −1, `rlSetUniform` ignored them silently, and the threshold stayed 0: the GPU path of every `Simulation` ran without cell mutation while the CPU path mutated. The stepper-level equivalence tests never move a stepper and could not see it; the Simulation-level test that should have caught it earlier happened to compare GPU against GPU on the checks that mattered.
**Reproduction.** Create a `Simulation` on each path, set `p = 0.05`, step once, compare.
**Notes.** State is now split into `Owned` (GL handles, exchanged on move) and `Config` (plain data, copied wholesale), so a new field cannot be forgotten. `tests/sim/simulation_test.cpp` has a moved-stepper regression case.

### BUG-008: default random-fill densities sum to more than one above nine states
**Status:** fixed
**Found:** 2026-09-14 (D-016, rendering the fourteen-state cyclic rule)
**Fixed:** 2026-09-15
**Location:** was `src/ui/app.cpp` (`applyPaletteForStates`) and `src/ui/headless.cpp`; now `src/sim/fill.cpp` (`defaultDensity`)
**Severity:** low
**Description.** The default fill gives state 1 a density of 0.2 and every other state 0.1, so for `S > 9` the weights sum past 1. `fillRandom` walks the cumulative distribution and stops at the first threshold above its draw, so the last states are never seeded: a fourteen-state rule starts with nothing in states 10 to 13. The engine is doing what it was asked; the defaults are wrong.
**Reproduction.** `aether --rule @cyclic-14` and look at the initial grid, or press R.
**Notes.** Recorded while open: a one-line fix, but the author's call whether the default should be uniform or weighted toward the quiescent state. Does not affect reproducibility either way — the densities are journaled and replay exactly.
**Resolution (2026-09-15, commit `aa66b59`).** Uniform was chosen. `sim::defaultDensity` replaces three copies of the same defaulting arithmetic in `app.cpp` (twice) and `headless.cpp`. The default is now an even spread over the states the rule lives in — `1/live` each, so state 0 takes the same share — with a two-state rule keeping the conventional 30% Life soup, and the ageing tail of SPEC §7 left empty, since a half-faded cell is not a sensible thing to start a run with. The weights therefore sum to at most one for any state count. The Grid panel shows what state 0 is left with, warns when hand-set sliders total more than one, and has an "even spread" button to put them back. Which default is right remains the author's to revisit, and it is now one function rather than three copies.

### BUG-009: the application writes its interface state into the working directory
**Status:** fixed
**Found:** 2026-09-15 (noticed in the file list of the interface commit)
**Fixed:** 2026-09-15
**Location:** `src/ui/app.cpp` (`configDirectory`, `run`); `.gitignore`
**Severity:** low
**Description.** Dear ImGui saves the layout it remembers to `imgui.ini` beside whatever directory the binary was launched from, and nothing had told it otherwise. So a run from the project root left a file in the repository — where it was committed by accident in `eb1889f` and then rewrote itself on every subsequent run, dirtying the tree — and a run from anywhere else littered that directory too.
**Reproduction.** `cd /tmp && aether --frames 1`, then look for `/tmp/imgui.ini`.
**Notes.** The path string has to outlive the call, since ImGui keeps the pointer rather than a copy — a local `std::string` there would be a use-after-free rather than a stray file.
**Resolution (2026-09-15, commit `74f54e5`).** `IniFilename` points at `$XDG_CONFIG_HOME/aether/imgui.ini`, falling back to `~/.config/aether`; the file is untracked and ignored, and a run from an arbitrary directory now leaves nothing behind.

### BUG-010: a growth expression cannot read the convolution result it is a function of
**Status:** fixed
**Found:** 2026-09-16 (Phase 5 step 2, authoring the first kernel)
**Fixed:** 2026-09-16
**Location:** `src/rule/ir.cpp` (`checkExpression`, the `ExprOp::Self` case); SPEC.md §6
**Severity:** medium
**Description.** A `Kernel`'s growth function is documented in the IR as an expression whose `Self` is the convolution result, and validation requires it to produce a `Float`. But `checkExpression` types `ExprOp::Self` as `Int` unconditionally, and the numeric operators require both operands to share a type, so any expression combining `Self` with a float literal is ill-typed. The only growth expressions that validate are those built purely from `FloatLiteral` arithmetic — that is, constants, which ignore the convolution result entirely and are not growth functions. The rule is therefore unsatisfiable in every useful case, and has been since the IR was written.
**Reproduction.** Build a `Kernel` whose growth is `Sub(Mul(FloatLiteral 2, Self), FloatLiteral 1)` and call `validate`: it reports that the growth expression must produce a float, because the subtree containing `Self` typed as `Int`.
**Notes.** Unreached until now because nothing constructed a `Kernel`: both backends refuse `f32` and neither front end could emit one. Phase 5 step 2 is the first thing to try. The fix is context-dependent typing — inside a growth expression `Self` is the convolution result and is `Float`, while everywhere else it stays the own state and is `Int` — which is a change to SPEC §6's typing rules rather than to the IR's data layout. It blocks kernel authoring completely, so it cannot be deferred past step 2 without leaving the step undeliverable.
**Resolution (2026-09-16).** `ExprContext` gained a `selfIsFloat` flag, set only where a growth expression is checked, so `Self` types as the convolution result there and as the own state everywhere else. SPEC §6 states the rule. The fix is to the typing of an operator rather than to the IR's data layout, so no schema change and no `ir_version` bump. `tests/rule/ir_test.cpp` had asserted the defect as intended behaviour — a growth function of `Self` alone was expected to be refused — which is how it survived being written; it now checks that the identity growth function is accepted and uses a Bool-valued expression to exercise the diagnostic.

## Won't Fix

*None.*

## Deferred

*None.*
