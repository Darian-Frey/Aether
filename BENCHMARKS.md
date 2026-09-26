> **Status:** Baselines taken 2026-09-25 at commit `f623eaf`
> **Machines:** ThinkPad P15 Gen 2i — NVIDIA T1200 4 GB (the target), Intel UHD integrated (for contrast), Linux 6.17
> **See also:** [SPEC.md](SPEC.md) §12 for the acceptance thresholds these are measured against

# Benchmarks

SPEC §12 sets thresholds. This file records what the machine actually does, so that a change which halves the throughput is noticed as a change rather than as a number that was always like that.

Every figure below was produced by [`scripts/benchmark.sh`](scripts/benchmark.sh) or by the commands quoted beside it. **All of them are T1200 figures** unless they say otherwise: SPEC §12 is a T1200 document, and a measurement from the integrated GPU is not comparable to it.

```bash
AETHER_PRIME=1 ./scripts/benchmark.sh ./build/aether
```

---

## Method

Each configuration is run twice, at a short and a long generation count, and **the rate comes from the difference**. Starting a process, opening a window, compiling a rule and seeding a grid costs about 100–220 ms on this machine — charged to a 2,000-generation run that is a 10% error, and to a 50-generation run it is most of the measurement. Differencing removes it exactly, because it is the same on both runs of a pair.

Rates are the best of several pairs. The best rather than the mean because the thing being measured is what the machine can do, and every disturbance — another process, a thermal dip, the GPU still clocking up — pushes in one direction only.

**The noise floor is about 4%.** Five identical pairs at 1024² gave 4929, 4755, 4746, 4739 and 4744 gen/s. The first is the outlier, and repeatedly so: it is the GPU coming up to clock. A difference smaller than 5% between two configurations means nothing here.

It is wider for the small configurations. The 1D rows spread about 8% — five pairs at 16,384 cells gave 87108, 83612, 80386, 85324 and 82781 — because a generation is so little work that the fixed cost of dispatching it is most of the time, and that cost is what varies. Those two rows are given to two significant figures for that reason; the rest are single measurements of a stable quantity.

---

## Against the acceptance thresholds

| SPEC §12 target | Threshold | Measured | |
|---|---|---|---|
| 2D binary, 1024², LUT backend | ≥ 200 gen/s | **4,955 gen/s** | 25× |
| 2D binary, 1024², CPU reference | ≥ 5 gen/s | **24 gen/s** | 5× |
| 3D binary, 256³, LUT backend | ≥ 30 gen/s | **69 gen/s** | 2.3× |
| 3D volume render, 256³ | ≥ 30 fps | **49.7 fps** | 1.7× |
| Rule mutation event, table backend | < 1 ms | **under 0.1 ms** | see below |
| Rule mutation event, codegen, cache miss | < 250 ms | **64 ms** | 3.9× under |
| Cell mutation at `p = 0` | < 2% cost | **no cost by construction** | see below |
| VRAM, 256³ `u8` grid pair | ≤ 40 MB | **32.00 MiB** | |

Every threshold is met. The two with the least headroom are the 3D ones, which is the expected shape: a 256³ grid is sixteen million cells stepped and then raymarched.

---

## Throughput

| Configuration | T1200 | Intel UHD | ratio |
|---|---|---|---|
| 2D binary 1024², table backend | 4,955 gen/s | 310 gen/s | 16× |
| 2D binary 1024², CPU reference | 24 gen/s | 26 gen/s | — |
| 2D 16-state 1024², codegen backend | 2,588 gen/s | 248 gen/s | 10× |
| 2D binary 1024², cell mutation `p = 0.02` | 4,019 gen/s | 308 gen/s | 13× |
| 3D binary 256³, table backend | 69 gen/s | 6 gen/s | 12× |
| 1D elementary, 16,384 cells | ~84,000 gen/s | 13,499 gen/s | 6× |
| 1D elementary, 32,768 cells | ~50,000 gen/s | *refused* | |
| Continuous 512², kernel radius 4 (80 neighbours) | 1,977 gen/s | 124 gen/s | 16× |
| Continuous 512², kernel radius 13 (728 neighbours) | 225 gen/s | 13 gen/s | 17× |

**Re-measured 2026-09-26, after BUG-021.** Fixing that defect required SPEC §6's fourth agreement rule — a subnormal float is flushed to zero after every float operation — and its cost falls on the continuous rows, which are the float-heaviest thing here. Measured by the same two-point method on the T1200 rather than assumed, in two stages, because the fix landed in two:

| | recorded above | expressions flushed | convolution flushed too |
|---|---|---|---|
| Continuous 512², radius 4 | 1,977 gen/s | 2,089 | **1,961** |
| Continuous 512², radius 13 | 225 gen/s | 226 | **213** |

Flushing the generated expressions costs nothing measurable, which is the expected shape: a growth function is a handful of operations against a convolution of eighty or 728. Flushing the convolution's partial sums costs the radius-13 row **5.3%** against the recorded figure, outside the 4% noise floor and therefore a real change; radius 4 loses 0.8%, inside it. The inner loop is where the work is and where the flush is now done per iteration, so this is the cost landing where it should.

It is paid deliberately. The alternative is a continuous rule whose two paths agree only as long as the field never decays towards zero, which is a promise about the rules that happen to be bundled rather than about the engine (AV-015). No SPEC §12 threshold covers the continuous path, so nothing is at risk; the figure is recorded here so that a later reading of 213 is not mistaken for a regression. The 2,089 in the middle column is 5.7% *above* the recorded figure and cannot be an effect of adding work — it is the noise floor showing itself, and the reason the table above is left as the 0.1.0 measurement rather than revised on one run.

The CPU reference is the same on both, as it must be — it is the same code on the same processor, and the row is there to show that the harness is measuring what it claims to.

**The integrated GPU misses one threshold.** 3D at 256³ manages 6 gen/s against a target of 30. Everything else clears: 2D is 310 gen/s against 200, which is comfortable but not by much. SPEC §12 is a T1200 document and the integrated figures are recorded for contrast rather than as a second acceptance run, but it is worth knowing that a 3D grid at full size is not usable on the integrated GPU rather than merely slower.

**Cell mutation is free on the integrated GPU** — 308 gen/s against 310, well inside the noise — where it costs 15% on the T1200. The consistent reading is that the integrated part is bandwidth-bound and the extra hashing fits in the time it is already waiting for memory, while the T1200 has the bandwidth and feels the arithmetic. That is an inference from two numbers, not something that was tested.

### Rendering

| | T1200 |
|---|---|
| 3D volume render, 256³, stepping as it draws | 49.7 fps |
| 2D palette pass, 1024² | 61.4 fps — **vsync-capped, not GPU-bound** |

The 2D figure is the monitor, not the renderer: vsync is on and the frame arrives with time to spare. It is recorded only so that nobody reads it as a measurement of the palette pass. The 3D figure is below the cap and therefore real.

---

## Notes on particular measurements

**Cell mutation at `p = 0` costs nothing, and cannot.** `setCellMutation(0)` gives a threshold of zero, and both steppers skip the whole branch when the threshold is zero — so `p = 0` is not a cheap mutation, it is no mutation. The measured difference between a run with `--cell-mutation 0` and one without is 2.3%, which is inside the 4% noise floor, because the two runs are executing identical code.

At `p = 0.02` the cost is real and larger than SPEC §12's parenthetical suggests: 4,019 gen/s against a 4,721 baseline measured in the same session, which is **15% of the rate** (17% more wall-clock time), not the ~10% recorded on 2026-09-11. Three pairs each, and the baseline was tight — 2118, 2116 and 2125 ms for 10,000 generations. The drain every 64 generations that fixed BUG-011 landed between those two measurements and is the obvious suspect, but that is a guess and has not been tested.

**A table-backend rule mutation is too cheap to measure this way.** 300 mutations at 256² — one per generation, 281 of them distinct — cost 109 ms against a 150 ms baseline, which is to say the run with 300 mutations was *faster* than the one without. Both figures are dominated by ~100 ms of process start-up. All that can honestly be said is that 300 events are lost inside a few tens of milliseconds of noise, so an event costs well under 0.1 ms, against a budget of 1 ms.

**A codegen cache miss is 64 ms**, and that one measures cleanly because it is large. The same experiment on the 16-state expression rule: 300 generations with a mutation every generation took 18,117 ms against 153 ms unmutated, and the session's lineage records 280 distinct `ir_hash` values — so 280 shader compiles for 17,964 ms, or 64 ms each. It agrees with the 61 ms noted in passing during Phase 4.

Attempts to measure this with fewer events gave 15, 8.8 and 40 ms per event, which is not a per-event cost at all: **the program cache is keyed on `ir_hash`**, so a mutation that lands on a rule already compiled is free, and with a handful of events the mix of hits and misses decides the answer. Only at a scale where the misses can be counted does the figure mean anything.

**The widest 1D grid is the driver's texture limit**, not a limit of Aether's: a 1D grid is stored as a texture one row tall, and `GL_MAX_TEXTURE_SIZE` bounds its width. That is 32,768 on the T1200 and **16,384 on the Intel UHD** — so a 1D size that runs on the discrete GPU may be refused on the integrated one, on the same machine. It is refused rather than truncated, and the message names the limit it found: `extent 32768 exceeds the driver's 2D texture limit of 16384`. The 16,384 row above is measured on both for that reason.

**A grid above 4 MB writes a sidecar file beside the session**, so `--save /dev/null` fails for the larger configurations with `cannot open /dev/null.grid for writing`. The benchmark script saves into a temporary directory instead; the write is identical on both runs of a pair and cancels out of the difference.

---

## Reproducing these

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
AETHER_PRIME=1 ./scripts/benchmark.sh ./build/aether
```

`AETHER_PRIME=1` selects the discrete GPU on an Optimus laptop. Without it the figures are the integrated GPU's and are not comparable to SPEC §12. `REPEATS=5` takes more pairs per configuration.

The measurements the script does not cover, because they need a window or a lineage to count:

```bash
# 3D volume render: 180 frames between the two, wall clock
aether --rule B5/S45 --size 256x256x256 --frames 60
aether --rule B5/S45 --size 256x256x256 --frames 240

# codegen cache misses: distinct ir_hash values in the lineage is the miss count
aether headless --rule "$(cat scripts/bench16.rule)" --size 256x256 \
       --generations 300 --rule-mutation 1:1 --save m.aether
```

The fixtures the script uses — `scripts/bench16.rule` for the codegen backend and `scripts/bench-r4.lua` / `scripts/bench-r13.lua` for the convolution — are chosen to exercise one thing each and are not rules worth watching. The 16-state rule asks about two states per own state deliberately: asking about one would take D-016's counted form and a 144-entry table, which is the opposite of what it is there to measure.
