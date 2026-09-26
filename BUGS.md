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

*None.*

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

### BUG-011: a long run without synchronisation comes back wrong
**Status:** fixed
**Found:** 2026-09-17 (Phase 5 step 5, running the bundled Lenia rule at the size SPEC §12 asks for)
**Fixed:** 2026-09-17
**Location:** `src/sim/gpu_step.cpp` (`GpuStepper::step`), `src/core/gpu_grid.cpp` (`GpuGrid::download`)
**Severity:** high
**Description.** A run that queues compute dispatches without ever synchronising eventually produces wrong results, on both drivers and in different ways. Running `rules/lenia.lua` at 512² through `aether headless`: Mesa returned a grid of exactly zero from generation 400 onward, having been healthy at 350; the T1200 stayed healthy far longer and then came back at generation 5000 with the field collapsed to four cells, four `0xFFFFFFFF` quiet NaNs at (0,0) through (3,0), and one wildly out-of-range value at (8,0) that varied between runs. Smaller grids failed the same way later — 256² on Mesa was healthy at generation 1000 and empty at 4000 — so it is cumulative rather than a function of size.
**Reproduction.** Before the fix:
```
aether headless --lua rules/lenia.lua --size 512x512 --generations 400 --seed 3 --save a.aether
```
returned an empty grid on Mesa, where `--generations 350` returned a healthy one.
**Notes.** The false leads are worth recording, because each looked convincing.

- *It is continuous-specific.* It is not: a discrete rule with the same 440-neighbour kernel at 512² showed it too. Life at 1024² never did, which is why nothing had caught it — the discrete rules that get run at scale all have eight neighbours, and a light dispatch drains before the next arrives.
- *The barrier bits are insufficient.* They are not. `GL_ALL_BARRIER_BITS` between dispatches changes nothing, and neither does a barrier at download time.
- *It is a numerical blow-up in the automaton.* It is not. The CPU path holds 28.0% mass at 512² from generation 200 through 4600 without a wobble, and the NaNs never spread — a NaN in the grid would reach every cell within the kernel radius on the next generation, and after a thousand more the whole grid would be NaN. Four cells, unchanged. The corruption is therefore not in the simulation.
- *A NaN cannot come from that arithmetic.* It cannot, which is the clue rather than a puzzle: the growth function is multiplies, subtractions and a select over finite inputs, with no division, ending in a clamp to [0, 1]. A value it cannot produce has to come from somewhere other than the step.

What settled it was the interface. The same rule at 512² in the window is perfectly healthy at generation 7721 on the T1200 and 632 on Mesa, both well past where headless was ruined — and the difference is that `App` calls `glFinish()` once a frame for the vsync throttle (a line added on 2026-09-11 for a timing reason entirely unrelated to this). The interactive path had been immune by accident all along.
**Resolution (2026-09-17).** `GpuStepper::step` drains the queue every 64 generations, and `GpuGrid::download` drains before reading back. Every 64 is as good as every one and the interval is not a tuning knob: without it the results are wrong rather than late. Downloads happen on save and on a path switch, never in the step loop (AV-002), so the second sync costs nothing. Mesa and the T1200 now agree with each other and with the CPU path at 512² where before they agreed with nobody.

### BUG-012: placing a pattern also paints a cell under the cursor
**Status:** fixed
**Found:** 2026-09-21 (reported by the author, with screenshots)
**Fixed:** 2026-09-21
**Location:** `src/ui/canvas.cpp` (`App::updateCanvas`)
**Severity:** medium
**Description.** A click that placed a pattern also painted one cell at the cursor. The placement branch consumed the *press* and cleared the pending pattern, then returned — but a click lasts several frames, and on the next one there was nothing pending, so control fell through to the paint branch, which starts a stroke on the button being *down* rather than pressed. With the brush at radius 0 that is exactly one cell, in the middle of the pattern just placed, which is what the screenshots showed.
**Reproduction.** Open a pattern, click the grid to place it, and look at the cell under the cursor.
**Notes.** The general shape: one gesture consumed by two handlers because one of them triggers on a press and the other on a hold.
**Resolution (2026-09-21).** Placing sets `swallowLeft_`, and the paint branch does nothing until the button is released. Cleared above the mouse-capture early return, or releasing over a panel would leave it set and swallow the next click on the canvas.

### BUG-013: a pattern the rule cannot take was refused invisibly
**Status:** fixed
**Found:** 2026-09-21 (reported by the author: "the wireworld loop will not draw the loop")
**Fixed:** 2026-09-21
**Location:** `src/ui/panels.cpp` (`App::drawPatternPreview`, `drawPatternsPanel`)
**Severity:** medium
**Description.** Selecting the bundled Wireworld loop while Conway's Life was running did nothing when clicked. The engine was right — a four-state pattern cannot go onto a two-state rule, and `placePattern` refused it — but the refusal went only to the log, which is a collapsed panel by default. The preview meanwhile stayed the same colour it uses for a pattern that *will* place, so a correctly refused pattern was indistinguishable from a broken one.
**Reproduction.** Run `--rule @life`, pick "Wireworld loop" from the Patterns list, click the grid.
**Notes.** The behaviour was correct and the feedback was not, which is the harder half to notice: nothing was logged as an error in the engine's own terms, so only a user could find it.
**Resolution (2026-09-21).** `Simulation::canPlace` is split out of `placePattern`, so the interface can ask the engine the same question before the click rather than forming its own opinion. The preview turns red for any reason the click would be refused, not only for hanging over an edge; the panel shows the reason in words; and a pattern the running rule cannot take is greyed in the library list, which stops the confusion before it starts.

### BUG-014: `[gpu]` cases skip at random, so a green suite does not mean the GPU path ran
**Status:** fixed
**Found:** 2026-09-22 (F-030, running the suite on both GPUs after the inspector landed)
**Fixed:** 2026-09-23
**Location:** `tests/support/gl_context.cpp` (`GlContext`, `requireGl`)
**Severity:** medium
**Description.** Each `[gpu]` case constructs a `GlContext`, which calls `InitWindow`, and skips itself if the window did not come up. That is right on a machine with no display. On a machine *with* one it is also firing, intermittently and in numbers. Seven runs of `[gpu]` on the Intel iGPU on 2026-09-22, same binary, no code change between any of them, skipped 2, 1, 3, 7, 9, 12 and 9 of the 49 cases — in that order, which is the order they were run in. The count climbing through a session points at something the process accumulates rather than at chance. The runs still report success, because a skipped Catch2 case is not a failure, so the suite says the GPU path is fine while a fifth to a quarter of the checks on it did not execute — the CPU/GPU equivalence cases among them. That is the one test the project's own build notes say nothing else is trustworthy without.
**Reproduction.** `for i in 1 2 3; do ./build/tests/aether_tests "[gpu]" | grep "^test cases:"; done` on a machine with a display, and again after several more runs. The counts differ between runs and tend to worsen. Both GPUs show it, so it is not the PRIME offload path. `[equivalence]` run on its own skipped nothing, which is consistent with the pressure theory and is also why the damage has not shown up as a wrong result yet.
**Notes.** The cause is not established. Opening and closing several dozen real windows in one process is the obvious suspect — raylib's `InitWindow` failing under some resource the process is not releasing between cases — and if so the fix is one shared context for the whole run rather than one per case, which would also make the suite faster. What matters more than the cause is that the failure is silent: whatever is done about the windows, `requireGl` should tell the difference between "there is no display, skip" and "there is a display and the context did not come up", and the second should fail rather than skip. As it stands a real GPU regression could sit behind a green run.
Not caused by the work it was found during: F-029 and F-030 add no GL and no `[gpu]` cases, and the case count is unchanged at 49. Logged rather than fixed, per the maintenance rule.
**Resolution (2026-09-23).** The cause, once the helper's `SetTraceLogLevel(LOG_NONE)` was lifted long enough to read the log, is GLX rather than anything of ours: `GLX: Error 65542: No GLXFBConfigs returned`, then `Failed to find a suitable GLXFBConfig`, then the window fails. Opening and closing a hidden window per case exhausts framebuffer-config enumeration after a few dozen cycles, which is why the count climbed through a session and why it affected both GPUs.
So there is now one context for the whole run, created on first use and deliberately never closed — whether raylib's state is still intact when a static destructor runs is not worth betting the suite's exit code on, and the process is ending anyway. That removed the failure entirely: five consecutive runs of the `[gpu]` set gave 51 of 51 cases and the identical assertion count every time, where the count had previously varied between 3,943 and 5,154 depending on how many cases had quietly not run. It also took about fifteen seconds off the suite, since the teardown was not free.
The silence was the other half and is fixed separately, as the notes asked: `requireGl` now distinguishes no display at all, which still skips, from a display that is present while the context will not come up, which now fails. Both branches were checked rather than reasoned about — with `DISPLAY` unset all 51 skip, and with `DISPLAY=:99` all 51 fail.
Nothing was hiding behind the skips: every case that had not been running passes.

### BUG-015: the editor window moved ImGui's cursor without submitting anything, flooding the terminal
**Status:** fixed
**Found:** 2026-09-22 (reported by the author, who saw the terminal filling up while using the editor)
**Fixed:** 2026-09-22
**Location:** `src/ui/editor.cpp` (`drawEditorGrid`, `drawNeighbourhood`)
**Severity:** low
**Description.** Both functions called `SetCursorScreenPos` to move past content they had drawn into the draw list by hand, and in each case something could follow it that submitted no widget. ImGui raises `Code uses SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries` for that, because a cursor moved past the content extent without an item behind it leaves the window unable to work out how big its content is. It fires once per offending call per frame, so at 60fps the terminal fills. Nothing was drawn wrongly and nothing crashed — the window sizes itself from the `InvisibleButton` and the `Dummy` either way — but a log line per frame drowns anything else the application has to say, which is the actual cost.
**Reproduction.** Open the editor (`E`) and leave it open. With the inspector reading a cell whose neighbourhood needs no legend, and with the cursor on the fringe of the pad rectangle, both sites fire. Measured over 120 frames: 117 error lines before, 0 after.
**Notes.** Both calls were redundant. `InvisibleButton` had already advanced the cursor past the pad, and `Dummy` had already reserved and advanced past each plane of the neighbourhood diagram — that is what makes `Dummy` the right tool for hand-drawn content and it was doing its job. Removing both is the whole fix; the hover readout also gained an else branch so that a frame on the fringe still submits a line and the layout does not jump.
Made visible rather than caused by two things landing the same day: the neighbourhood diagram was new, and the legend under it had just been made conditional, which is what let a frame reach the end of the loop with nothing following the cursor move.
**Resolution (2026-09-22).** Both `SetCursorScreenPos` calls removed, an `else` added to the hover readout. Verified by A/B rather than by reasoning: the calls were put back and the same 120-frame run produced 117 errors, then removed again for 0.

### BUG-016: the preview texture outlived the GL context and segfaulted on exit
**Status:** fixed
**Found:** 2026-09-23 (IMP-008, first run with a pattern open)
**Fixed:** 2026-09-23
**Location:** `src/ui/app.cpp` (`App::run` teardown), `src/ui/app.hpp` (`previewGrid_`)
**Severity:** medium
**Description.** IMP-008 gave `App` a `core::GpuGrid` to hold the pending pattern as a state texture. `App::run` resets `sim_`, `renderer_` and `renderer3d_` before `CloseWindow()` precisely because a GL handle destroyed after context teardown segfaults; the new member was not added to that list, so it was destroyed in `~App()` instead, after the context was gone. Any run that opened a pattern crashed on exit. Nothing was lost — the crash is after the last frame and after any screenshot — but a process that segfaults on the way out is not something to ship, and a user would reasonably read it as the pattern having broken something.
**Reproduction.** `aether --pattern some.rle --frames 30`, then let it exit. Backtrace: `GpuGrid::~GpuGrid` inside `App::~App` inside `main`.
**Notes.** The rule this breaks is already written down, in CLAUDE.md's pitfalls and in ATTACK_VECTORS: GL RAII objects must be destroyed inside the window's lifetime. It is worth noticing that having written the rule down did not prevent walking into it, because the rule is remembered rather than structural — nothing stops a GL-owning member being added to `App` without a matching reset. A `struct GlOwned { ... }` grouping every such member, reset in one place, would make the next one impossible rather than merely documented. That is a change to `App`'s shape and is the author's call, not something to fold into an improvement about pattern previews.
**Resolution (2026-09-23).** `previewGrid_.reset()` added alongside the others before `CloseWindow()`. Confirmed by three clean runs; the crash was reproducible on every run before it.

### BUG-017: `--rule @name` works interactively and not headlessly
**Status:** fixed
**Found:** 2026-09-23 (writing MANUAL.md, checking every example rather than assuming it)
**Fixed:** 2026-09-23
**Location:** `src/ui/headless.cpp` (`runHeadless`), `src/ui/app.cpp` (the search path)
**Severity:** low
**Description.** `--help` lists `--rule R` once, for every invocation, and says "@name loads from the library". That holds for the window, where `App::run` searches `rules/` and resolves the name before compiling. It does not hold for `aether headless`, which passes `opts.rule` straight to the DSL parser and gets `rule: 1:1: unexpected character '@'`. The same flag, documented once, behaves differently depending on the subcommand — and it fails in the path where a user is least likely to be watching, since headless is what goes in a script.
**Reproduction.** `aether headless --rule @wireworld --generations 5 --save w.aether` exits 1 with the parse error. `aether --rule @wireworld --frames 5` runs.
**Notes.** The library loader is `rule::loadLibrary` plus `rule::compileLibraryRule`, both already in `aether_rule` and both already used by `App`; `runHeadless` would need the same four lines and the same search path. The alternative is to narrow the help text to say the library is a window feature, which is smaller but leaves a scripted run unable to name a bundled rule — including Langton's loops, whose 219 clauses are not something anybody will paste onto a command line.
Found while writing the manual, which is the first thing to have tried every documented invocation in one sitting. Logged rather than fixed, per the maintenance rule; the manual documents what is true today and points here.
**Resolution (2026-09-23).** `runHeadless` resolves `@name` the way the window does, through `rule::loadLibrary` and `rule::compileLibraryRule`, so a Lua rule works by name as readily as a DSL one. The search path was the part worth being careful about: it had been written inline inside `App::run`, and a second copy in `headless.cpp` would have been a new way for the two to disagree — which is the shape of this bug, not merely its cause. It is now `ui::ruleSearchPath()`, with `patternSearchPath()` extracted beside it since they were the same four lines twice.
The rule also decides the dimensionality here, as it already did in the window: `--rule @rule110 --size 512x512` gives a 1D grid of 512 and `--rule @life-3d-4555 --size 32x32` a 32³ volume, each announced rather than silently reshaped.
Guarded by `library.*` under CTest, and guarded by comparison rather than by exit code: the same rule named and spelled out, same seed, same extent, fifty generations, and `compare` settles whether `@life` really is `B3/S23`. An unknown name is refused as an unknown name rather than handed to the parser to report as a stray `@`.

### BUG-018: F1 opens the key list in 3D only
**Status:** fixed
**Found:** 2026-09-23 (writing MANUAL.md, checking the shortcut rather than repeating the README)
**Fixed:** 2026-09-23
**Location:** `src/ui/canvas.cpp` (`updateCanvas`, the 2D branch)
**Severity:** low
**Description.** The README has said "**Keys** lists the shortcuts, and F1 opens it" since it was written, and the Keys section itself offers `F1` or `?`. Neither works in two dimensions. `updateCanvas` has two keyboard branches, one per dimensionality, and `KEY_F1 || KEY_SLASH` appears only in the 3D one — every other shortcut is in both. The section can still be opened by clicking its header, so nothing is unreachable; the documented way in simply does not work in the mode almost everyone uses.
**Reproduction.** Run 2D and press F1 or `?`: nothing. Run `--size 32x32x32` with a 3D rule and press F1: the Keys section opens.
**Notes.** One line, duplicated into the 2D branch beside the other shared shortcuts. It is worth asking why the two branches share thirteen keys by copy rather than by a common block, since that is the mechanism by which this went missing and would be the mechanism for the next one; that is a larger change than the fix and is the author's to weigh.
Found while writing the manual, because a documented shortcut is the kind of claim that should be tried rather than copied from the README that also asserts it. The manual now says what is true and points here.
**Resolution (2026-09-23).** Fixed by removing the duplication rather than by adding the missing line to it. `updateCanvas` kept two near-identical keyboard blocks, one per dimensionality, and the thirteen shortcuts common to both are now written once above the `is3D()` branch; each branch keeps only what is genuinely its own, which for 3D is the slice controls and for 2D is nothing. A shortcut that belongs to both dimensionalities now has one home and cannot be added to half of them.
Reading that function to make this change is what turned up BUG-019, which had been live for two days in the same block.

### BUG-019: placing a pattern in 2D kills painting for the rest of the session
**Status:** fixed
**Found:** 2026-09-23 (fixing BUG-018, reading the function the shortcut lives in)
**Fixed:** 2026-09-23
**Location:** `src/ui/canvas.cpp` (`App::updateCanvas`)
**Severity:** high
**Description.** BUG-012's fix added `swallowLeft_`, set when a pattern is placed and cleared when the left button comes up, so that one click could not both place and daub. The clear was put inside the `if (is3D())` branch instead of above it. In three dimensions everything works. In two — where patterns are actually placed, since placement is 2D only — the flag is set and never cleared, and `if (swallowLeft_) return;` at the top of the paint path then refuses every left click for the remainder of the run. Place one pattern and the brush is dead until you restart.
The same misplacement stranded the `Esc` cancel in the 3D branch, so a pending pattern cannot be cancelled by the key that the Keys list and the Patterns panel both say cancels it. Only clicking *Cancel* works.
**Reproduction.** Run 2D, open a pattern from the Patterns section, click the grid to place it, then try to draw. Nothing happens, and nothing is logged.
**Notes.** Found by brace-counting rather than by eye: the indentation in that region is misleading — the hoisted blocks are written at four spaces as though they were at function level, while the braces put them at depth one, inside the 3D branch. The compiler is satisfied either way. It arrived in the BUG-012 fix on 2026-09-21 and has been live since; the fix was confirmed working at the time, but what was confirmed was that placing no longer daubs, which is true, and not that painting still worked afterwards, which it did not.
This is the second defect in a fortnight caused by `updateCanvas` keeping two near-identical keyboard blocks, one per dimensionality, and it is the mechanism BUG-018's notes asked about. Thirteen shortcuts are duplicated between them by copy; a key or a guard added to one and not the other is invisible until somebody presses it.
**Resolution (2026-09-23).** The `swallowLeft_` clear and the `Esc` cancel are now above the `is3D()` branch, where the comments on both had always said they should be, so they run whatever the dimensionality. Fixed together with BUG-018 by giving the shared shortcuts one home instead of two copies, since that duplication is what produced both.
Verified structurally rather than by hand: brace-counting the function now puts the help toggle, the Esc cancel and the `swallowLeft_` clear above the branch, where before two of them were inside it. The behaviour itself — place a pattern, then paint; press Esc with one pending — needs a mouse and a keyboard, which a scripted run does not have, so it is worth a minute of somebody's hands.

### BUG-020: the Phase 7 entries are dated a day into the future
**Status:** open
**Found:** 2026-09-26 (writing F-031 step 2, dating the status line)
**Location:** `DECISIONS.md` (D-022), `FEATURES.md` (F-002, F-031), `SPEC.md` §1 and §6, `ROADMAP.md` Phase 7, `CHANGELOG.md`, `CLAUDE.md`
**Severity:** low
**Description.** Everything written in the session that opened Phase 7 is dated **2026-09-27**: D-022's *Decided* and *Recorded* lines and its author line, F-031's step 1 status, F-002's second correction, the two SPEC "added" notes, Phase 7's start in ROADMAP and the CHANGELOG paragraphs. That session ran on **2026-09-26**, which is also the day F-031 step 2 was written. The register therefore reads step 1 on the 27th and step 2 on the 26th: the two steps are in the wrong order, and D-022 is recorded as having been decided after the work that implements it.
Nothing is wrong in the code and no ID is affected. What is affected is the one thing the ISO 8601 convention exists for — being able to read the registers as a sequence — and the F-002 entry in particular now says a thing was closed "properly on 2026-09-27" a day before the date it carries.
**Notes.** Logged rather than corrected, per the convention: eleven dates across six documents, one of them the *Recorded* line of an accepted decision, is the author's call and not a tidy-up. The likely mechanism is that the session took its date from somewhere other than the clock and then propagated it consistently, which is why every entry agrees with every other and none agrees with the day.
F-031 step 2 is dated 2026-09-26, the day it was written, rather than being made to match its predecessor. Whichever way this is settled, it wants settling in one pass across all six files rather than one entry at a time.

### BUG-021: a subnormal float diverges between the oracle and the shader
**Status:** fixed
**Found:** 2026-09-26 (F-031 step 3, the first CPU/GPU comparison of a multi-field rule)
**Fixed:** 2026-09-26
**Location:** `src/rule/glsl.cpp` (`emitNodes`), `src/sim/cpu_step.cpp` (`evalArena`), SPEC §6
**Severity:** high
**Description.** Generated float code can produce a subnormal — a value below `FLT_MIN`, about 1.18e-38 — and GLSL does not require an implementation to support them. Both GPUs here flush a subnormal result to zero; C++ does not. The two paths then disagree, and the disagreement is not a rounding difference of an ULP but a value against zero, which compounds from the next generation on.
Found by the first multi-field equivalence comparison. The field's rule was `heat' = (heat + heat_east) * 0.5`, which halves whatever it is given: after 120 generations one cell reached `1.0e-38`, the oracle kept it and the shader had already made it zero. Neither is wrong on its own terms, which is the whole of AV-015 — being more accurate than the shader is the same defect as being less.
**Reproduction.** `tests/sim/fields_step_test.cpp`, the both-paths case. Before the fix it failed at generation 120 on cell 24 of field 1, on both the iGPU and the T1200, with and without cell mutation.
**Notes.** Not new with F-031. Any `f32` expression can reach the subnormal range, so the continuous path has carried this since Phase 5 — `rules/lenia.lua` never triggered it because a Lenia field holds itself in `[0, 1]` and its growth arithmetic does not decay towards zero, and the 10,000-generation comparison that passed at 512² is evidence about that rule rather than about the arithmetic. It would have waited for the first continuous rule that damps.
**Resolution (2026-09-26).** Flush-to-zero on subnormals, applied explicitly after every float operation in both twins, and recorded as SPEC §6's fourth agreement rule. Defining it here rather than deferring to the driver makes it robust in both directions: a driver that flushes finds the value already zero, and one that does not gets the same zero the oracle produced. A driver that flushes an *input* cannot matter either, because no input is ever subnormal by the time it is read.
It reaches further than the generated expressions, which is the part worth recording. Three float computations sit outside them and all three had to be covered: the convolution's running sum and each `weight × value` term, which is the likeliest place of all to reach the subnormal range; and `self + increment`, the last operation before the clamp, which is exactly where a value that is decaying to nothing lands. Fixing only the expression temporaries would have left the continuous path divergent while reading as though it were fixed — so the number is written once, in `rule/glsl.hpp`, and `sim/gpu_step` turns it into the `AETHER_FTZ` macro both shaders use.
The cost is one compare and select per float operation, which falls on the continuous path's inner loop. Measured rather than assumed — see BENCHMARKS.md's note of 2026-09-26.

### BUG-022: the pattern editor steps a multi-field rule past the end of nothing
**Status:** fixed
**Found:** 2026-09-26 (F-031 step 5, adding the multi-field fixture to the equivalence sweep)
**Fixed:** 2026-09-26
**Location:** `src/sim/scratch.cpp` (`Scratch::make`, `Scratch::setRule`), `src/sim/inspect.hpp`
**Severity:** high
**Description.** `sim::Scratch` is the pattern editor's pad: one `HostGrid`, no field storage, because a pattern is states (SPEC §14). `ui/editor.cpp` opens the pad with the *running* rule, and F-031's step 4 had just made a multi-field rule installable in a `Simulation`. So pressing `E` with such a rule running compiled it into the pad and called `cpuStep` with no field buffers at all. `cpuStep` asserts that it has one pair per declared field — and an assert is compiled out of a Release build, which is what ships. The result is a read past an empty span: a segfault if the read lands badly, and cells computed from whatever was there if it does not, which is the worse of the two.
Arrived with step 4 on 2026-09-26 and lived for one commit. It was not reachable before, because `Simulation::installRule` refused a field rule outright.
**Reproduction.** Found by the equivalence sweep rather than by the interface: adding a multi-field fixture put a field rule into `fixtures()`, and the inspector cases iterate that list and call `sim::inspect` with a state grid and nothing else. `SIGSEGV`, in a build with asserts enabled, at the first cell.
**Notes.** The mechanism is worth carrying: an `assert` is the house style for a precondition inside the engine and the aliasing check of AV-004 is one too, but it protects a debug build and documents a Release one. What actually keeps a caller honest is there being no way to reach the bad call, so the fix is a refusal at the boundary rather than a louder assert.
Widening the pad to hold fields was the alternative and was rejected: a pattern is states, the pad exists to draw one, and a field the editor could paint but no format could carry would be a dead end. The inspector is the same boundary — it reads the pad, which is D-018's option B.
**Resolution (2026-09-26).** `Scratch::make` and `Scratch::setRule` refuse a rule that declares auxiliary fields, with a message the editor logs, so opening the pad on a multi-field rule says so instead of crashing. `sim/inspect.hpp` records that it is state-only and why. The equivalence sweep's inspector cases skip fixtures with fields rather than being handed a rule they cannot describe.

## Won't Fix

*None.*

## Deferred

*None.*
