> **Status:** Current for 2026-09-24
> **Audience:** people writing rules for Aether in Lua. Some programming assumed; no Lua and no C++ needed — the language used here is a handful of tables, loops and arithmetic.
> **See also:** [MANUAL.md](MANUAL.md) for the application, [SPEC.md](SPEC.md) §8 for the contract stated formally, [DECISIONS.md](DECISIONS.md) D-003 for why Lua works the way it does here.

# Writing rules in Lua

Aether's own notation covers a lot — birth and survival counts, ageing tails, signature literals — but it runs out when a rule is easier *computed* than *written down*. A fourteen-state cyclic automaton is fourteen nearly identical lines in the notation and four lines in Lua. A Lenia kernel is a few hundred numbers nobody wants to type. That is what the Lua front end is for.

Every example here has been run. Where a claim could be checked, it was: the Life below is bit-for-bit the same automaton as `B3/S23`, and the error messages are copied from real output rather than paraphrased.

---

## Contents

- [The shape of a rule](#the-shape-of-a-rule)
- [Your first rule](#your-first-rule)
- [How a transition is asked for](#how-a-transition-is-asked-for)
  - [outer_totalistic](#outer_totalistic-the-default) · [counted_totalistic](#counted_totalistic) · [non_totalistic](#non_totalistic) · [totalistic](#totalistic) · [an array](#an-array-instead-of-a-function)
- [Dimensions and lattices](#dimensions-and-lattices)
- [Continuous rules](#continuous-rules)
- [Saving a rule into the library](#saving-a-rule-into-the-library)
- [The sandbox, and why](#the-sandbox-and-why)
- [When it goes wrong](#when-it-goes-wrong)
- [Recipes](#recipes)
- [Things Lua cannot do here](#things-lua-cannot-do-here)

---

## The shape of a rule

A script is a Lua chunk that **returns a table**. That is the whole interface. It runs once, when you compile, and then it is gone — see [the sandbox](#the-sandbox-and-why) for why that matters more than it sounds.

| Field | | |
|---|---|---|
| `states` | required, 2–256 | how many states a cell can hold. Omitted only for continuous rules |
| `neighbourhood` | required | `{ type = "moore" \| "von_neumann" \| "hex", radius = N }` |
| `transition` | required | a function, or an array — see [below](#how-a-transition-is-asked-for) |
| `kind` | optional | `"outer_totalistic"` (default), `"counted_totalistic"`, `"non_totalistic"`, `"totalistic"`, `"continuous"` |
| `dimensions` | optional | 1, 2 or 3. Defaults to whatever the session is |
| `boundary` | optional | `"wrap"`, `"zero"` or `"mirror"`. Defaults to the session's |
| `cell_type` | optional | `"u8"` (default) or `"f32"` for a continuous rule |
| `counted` | required for `counted_totalistic` | a list of states, or a function of the own state returning one |
| `kernel`, `growth` | required for `continuous` | see [continuous rules](#continuous-rules) |
| `metadata` | optional | `{ name = "...", author = "..." }`. Only those two |

Run one with `--lua`, or paste it into the Rule panel with the dropdown set to Lua:

```bash
./build/aether --lua myrule.lua --size 512x512
```

---

## Your first rule

Conway's Life, written out:

```lua
return {
    states = 2,
    neighbourhood = { type = "moore", radius = 1 },
    transition = function(own, counts)
        local live = counts[1]
        if own == 1 then
            if live == 2 or live == 3 then return 1 else return 0 end
        else
            if live == 3 then return 1 else return 0 end
        end
    end,
}
```

`transition` is called once per table entry while the rule is compiled, and is handed the cell's own state and the counts of its neighbours. `counts[1]` is how many neighbours are in state 1; `counts[0]` is how many are in state 0, which is there when you want it and ignored here.

This really is `B3/S23` — not approximately. Run each for sixty generations from the same seed and compare the sessions:

```bash
aether headless --lua life.lua   --size 96x64 --seed 7 --generations 60 --save lua.aether
aether headless --rule B3/S23    --size 96x64 --seed 7 --generations 60 --save dsl.aether
aether compare lua.aether dsl.aether
# identical: 6144 u8 cells at generation 60, 1 lineage entries
```

That is worth doing whenever you write a Lua version of something you can also express in the notation. It is the cheapest correctness check available.

---

## How a transition is asked for

Aether builds the whole lookup table at compile time by calling your function once per entry. **Which arguments it passes depends on the `kind`** — that is the one thing to get straight, and the rest follows.

Pick the kind by what your rule actually asks about. It decides the table's size, and a rule that asks for less gets a smaller table and a faster compile.

### `outer_totalistic` (the default)

`f(own, counts)` — the own state, and a count per state.

```lua
transition = function(own, counts)
    -- counts[0] .. counts[S-1], indexed from zero
end
```

Use it when the rule cares how many neighbours are in each of several states. It is the default because it is the most generally useful, and it is the biggest of the totalistic tables.

### `counted_totalistic`

`f(own, k)` — the own state, and **one** number: how many neighbours fall in the set this state counts.

Most real rules ask about exactly one state, and this is enormously smaller for them. A fourteen-state cyclic automaton needs 2.8 million entries as `outer_totalistic` and 126 as `counted_totalistic`.

```lua
local STATES = 6
return {
    states = STATES,
    neighbourhood = { type = "moore", radius = 1 },
    kind = "counted_totalistic",
    counted = function(own) return { (own + 1) % STATES } end,
    transition = function(own, k)
        if k >= 1 then return (own + 1) % STATES end
        return own
    end,
}
```

`counted` says what each state counts. It can be a plain list when every state counts the same thing — Brian's Brain is `counted = { 1 }` — or a function of the own state when they differ, as the cyclic rule above needs. Nothing can be inferred from the transition function, so this field is required.

### `non_totalistic`

`f(own, neighbours)` — the own state, and each neighbour individually, **one-based**, in the canonical order of SPEC §3.

```lua
kind = "non_totalistic",
transition = function(own, n)
    return n[2]        -- become whatever the west neighbour is, so the grid drifts east
end,
```

For 2D von Neumann radius 1 the canonical order is north, west, east, south — so `n[2]` above is the cell to the west, and copying it moves everything one cell east each generation. Check the order for your neighbourhood before relying on it; it is SPEC §3's, not an obvious one. This is the biggest table of all — `S^(N+1)` entries — so it is practical only for small state counts and small neighbourhoods. Eight states on a Moore neighbourhood would want 134 million entries and is refused.

### `totalistic`

`f(sum)` — one argument, the sum of the cell and all its neighbours.

```lua
kind = "totalistic",
transition = function(sum) return sum % 2 end,
```

The smallest table there is. Use it when the rule genuinely only cares about a total.

### An array instead of a function

If you would rather compute the whole table yourself, hand over an array of exactly the right length:

```lua
-- outer_totalistic, 2 states, von Neumann r=1: 2 x 5 = 10 entries.
local t = {}
for own = 0, 1 do
    for live = 0, 4 do
        local born  = (own == 0 and live == 3)
        local lives = (own == 1 and (live == 2 or live == 3))
        t[#t + 1] = (born or lives) and 1 or 0
    end
end
return {
    states = 2,
    neighbourhood = { type = "von_neumann", radius = 1 },
    transition = t,
}
```

The order is own-major, then the count vector, as SPEC §5 lays it out. The length must match exactly or the compile fails saying what it wanted.

The function form is almost always better: it needs no knowledge of the layout, and a wrong layout is a rule that compiles and misbehaves. Use the array when you are importing a table from somewhere that already has one.

---

## Dimensions and lattices

A rule can declare its own dimensionality, and the grid is rebuilt to match:

```lua
return {
    dimensions = 3,
    states = 2,
    neighbourhood = { type = "moore", radius = 1 },
    metadata = { name = "3D Life 4555" },
    transition = function(own, counts)
        local live = counts[1]
        if own == 1 then
            if live == 4 or live == 5 then return 1 else return 0 end
        else
            if live == 5 then return 1 else return 0 end
        end
    end,
}
```

`neighbourhood.type = "hex"` gives six neighbours on a hexagonal lattice, two-dimensional only. Everything else is unchanged — `counts[1]` still means what it meant:

```lua
return {
    states = 2,
    neighbourhood = { type = "hex", radius = 1 },
    transition = function(own, counts)
        local live = counts[1]
        if own == 1 then return (live == 3 or live == 4) and 1 or 0 end
        return live == 2 and 1 or 0
    end,
}
```

Omit `dimensions` and the rule takes the session's, which is usually what you want for a 2D rule.

---

## Continuous rules

A continuous cell holds a value in [0, 1] rather than a state index. There is no table and no counting: the neighbourhood is convolved with a **kernel**, and the result goes to a **growth function** that says how much to add.

This is the kind Lua is least avoidable for, because the kernel is hundreds of numbers computed from a formula.

```lua
-- A Gaussian shell, sampled by the script, and a growth band.
local RADIUS = 12
local profile = {}
for i = 0, RADIUS do
    local r = i / RADIUS
    profile[#profile + 1] = math.exp(-((r - 0.5) ^ 2) / (2 * 0.15 ^ 2))
end

return {
    cell_type = "f32",
    kind = "continuous",
    neighbourhood = { type = "moore", radius = RADIUS },
    kernel = { shape = "radial", profile = profile },
    growth = { form = "polynomial", mu = 0.15, sigma = 0.015, dt = 0.1 },
    metadata = { name = "Ring" },
}
```

- **`cell_type = "f32"`** is what makes it continuous, and such a rule has **no `states`** — saying both is an error, because an f32 cell holds a value rather than an index.
- **`kernel.shape`** is `"radial"` (samples from the centre outward, which Aether maps onto the neighbourhood by distance) or `"explicit"` (one weight per cell, row-major). The weights are normalised to sum to 1 for you.
- **`growth.form`** is `"rectangular"` or `"polynomial"`. `mu` is where growth peaks, `sigma` how wide the band is, `dt` how much of a step is applied per generation — Lenia's `1/T`, defaulting to 1.

Note `math.exp` above. You may use it **here**, because the kernel is sampled once at compile time on the CPU and the resulting numbers go into the rule. You may *not* have a growth function that needs `exp` at run time: that would have to be evaluated by the GPU, whose transcendental precision the driver decides, and the reference implementation would stop agreeing with it. That is why the growth forms are a closed set of two and not an expression you write.

---

## Saving a rule into the library

Put the file in `rules/` with a header in Lua comments, and it appears in the Library panel and answers to `--rule @name`:

```lua
-- name: Cyclic CA (6 states)
-- description: Each state is eaten by the next; spirals form out of noise.
local STATES = 6
return { ... }
```

`# name:` for a `.rule` file, `-- name:` for a `.lua` one — the header is a comment in whichever language the file is, so the file is compiled whole and nothing has to be stripped. `description` shows in the panel. A `palette` line works the same way for both.

---

## The sandbox, and why

A script may use `math`, `string`, `table`, `ipairs`, `pairs`, `select`, `tonumber`, `tostring`, `type`, `error` and `assert`. That is the entire environment.

There is no `io`, no `os`, no `require`, no `load`, no `debug` — and they are not merely deleted. The chunk runs with a fresh table as its environment, so the real globals were never reachable to begin with.

Two budgets apply: **50,000,000 VM instructions** and **256 MB**. The instruction budget is counted rather than timed, so where a runaway script stops does not depend on how fast your machine is. Exceeding either is an ordinary compile error, and the rule you were already running keeps going.

The reason for all of this is one sentence in [D-003](DECISIONS.md): **Lua runs at compile time only, and is not reachable from the step loop.** The interpreter is created and destroyed inside the compile call. Nothing Lua-owned outlives it. So a rule cannot make the simulation slow, cannot make it non-deterministic, and cannot read the clock or a file behind your back — not because it is policed, but because by the time the first generation is stepped there is no interpreter left to call.

It follows that your script is a *rule generator*, not a rule. It runs once. Loops, tables and recursion in it cost compile time and nothing else.

---

## When it goes wrong

Every message below is real output.

| What you wrote | What you get |
|---|---|
| no `states` | `the rule needs a 'states' count` |
| no `neighbourhood` | `the rule needs a 'neighbourhood' table of {type, radius}` |
| no `transition` | `the rule needs a 'transition'` |
| `kind = "magic"` | `unknown kind 'magic'` |
| `counted_totalistic` with no `counted` | `counted_totalistic needs a 'counted' field: a list of states, or a function taking an own state and returning one` |
| a transition returning 5 on a 2-state rule | `transition function returned 5 at own 0 with counts [0]; states run 0 to 1` |
| `io.write(...)` in the transition | `transition function failed at own 0 with counts [0]: rule:1: attempt to index a nil value (global 'io')` |
| 8 states, non-totalistic, Moore r=1 | `kind non_totalistic with 8 states and 8 neighbours needs 134217728 table entries, against a limit of 65536` |
| `while true do end` | `script exceeded the instruction budget of 50000000` |

Two habits worth having. Errors name the **arguments that produced them**, so `at own 0 with counts [0]` tells you which call to reason about. And a failed compile leaves the running rule alone — you can keep pressing Ctrl+Enter until it takes.

---

## Recipes

### Generate a family of rules from one parameter

```lua
local B, S = { [3] = true }, { [2] = true, [3] = true }
return {
    states = 2,
    neighbourhood = { type = "moore", radius = 1 },
    transition = function(own, counts)
        local live = counts[1]
        if own == 1 then return S[live] and 1 or 0 end
        return B[live] and 1 or 0
    end,
}
```

Changing the two sets at the top gives any Life-like rule. Sets read better than long `or` chains once there are more than two or three counts.

### Keep the table small on purpose

If your rule only asks about one state, say so with `counted_totalistic` and a `counted` set. The difference is not cosmetic: it decides whether the rule fits the lookup-table backend at all, and the table is built at compile time, so it is also the difference between an instant compile and a slow one.

### An ageing tail

There is no `decay` in Lua — it is a front-end convenience in the notation. Write the tail out; it is only ever a chain:

```lua
local TAIL = 4          -- states 2..5 are the fading ones
return {
    states = 2 + TAIL,
    neighbourhood = { type = "moore", radius = 1 },
    kind = "counted_totalistic",
    counted = { 1 },
    transition = function(own, k)
        if own == 0 then return k == 3 and 1 or 0 end
        if own == 1 then return (k == 2 or k == 3) and 1 or 2 end
        return own + 1 <= 1 + TAIL and own + 1 or 0
    end,
}
```

One thing the notation does that this does not: `B/S/C` records `decay_from` in the rule's metadata, which tells the palette to shade the tail and tells a fresh grid not to seed cells partway through dying. A Lua rule has no way to set it — `metadata` takes only `name` and `author` — so a Lua generations rule and its `B/S/C` twin are the *same automaton* with different default colours and a different random fill. Worth knowing before you conclude they disagree: give both the same starting cells and they step identically.

### Check your rule against one you trust

```bash
aether headless --lua mine.lua --size 96x64 --seed 7 --generations 60 --save a.aether
aether headless --rule B3/S23  --size 96x64 --seed 7 --generations 60 --save b.aether
aether compare a.aether b.aether
```

`compare` checks the cells and then the rule hashes, so it will tell you *which* of the two differs.

---

## Things Lua cannot do here

- **Run during the simulation.** Nothing you write is executed after the compile. This is not a restriction to be worked around; it is what the determinism guarantee rests on.
- **Return the `expression` kind.** The IR has one, and codegen executes it, but there is no way to build an expression tree from Lua — you would be writing an abstract syntax tree by hand. Use the notation's table blocks instead.
- **Read files, the clock, or randomness.** No `io`, no `os`, no `math.random` seeded from anywhere meaningful. A rule that varied between compiles would break session replay, which every other feature depends on.
- **Set a palette or a description from the table.** `metadata` takes `name` and `author`. Both of those belong in the file's header comments instead, where the library reads them.
- **Exceed the table limit.** 65,536 entries. If your rule needs more, it needs a smaller kind, a smaller neighbourhood, or fewer states — and the error tells you what it wanted.
