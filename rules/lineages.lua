-- name: Lineages
-- description: Every cell carries the Life-like rule it runs, as eighteen bits of genome, and a newborn inherits its parents'. Turn up the Inheritance mutation in the Mutation section to make variety, turn on "Colour by genome" in the Palette section, and watch rules compete. Starts from a genome that cannot reproduce at all, so everything you see was built by mutation and kept by selection.
-- palette: 1 = #B0C4DE
--
-- The first rule in the library that is not one rule (F-033). The transition
-- reads the cell's own genome and tests the bit the live-neighbour count
-- selects: nine bits of birth conditions, then nine of survival. That layout is
-- this rule's convention and not the engine's — the engine inherits eighteen
-- opaque bits and never learns what any of them means (D-025).
--
-- There is nothing to see until the genomes differ. A grid seeded with one
-- genome everywhere is that one rule and stays it, so the Mutation section's
-- Inheritance controls are where this rule comes to life: turn the per-bit
-- mutation up and drift produces variety, then turn it back down and watch which
-- variant takes the grid.
--
-- Measured, 64x64 from seed 9, `--inherit crossover:P`, at generation 1500. The
-- grid starts with a genome of all zeros, which is a rule that never births and
-- never survives, so everything here is built by mutation and kept by selection:
--
--   P = 0.0005   extinct. Too little variety to find a rule that reproduces
--                before the grid empties.
--   P = 0.002    3964 of 4096 cells alive, 600 distinct genomes, the commonest
--                holding 5% of the population.
--   P = 0.01     the grid full, 1372 distinct genomes, no genome above 1%.
--
-- Worth knowing what is being selected *for*. There is no cost to being alive in
-- this rule — no resource to eat, no lifespan — so the fittest genome is simply
-- the one that fills the most space, and the commonest survivor at P = 0.002 is
-- 0x3e17e: nearly every survival bit set. A grid of cells that never die is a
-- correct outcome and a dull one. Coupling a genome to the resource field of
-- F-032, so that filling the grid starves it, is what makes the competition
-- worth watching, and nothing in the engine prevents a rule declaring both.
--
-- The cleanest demonstration of selection alone is in
-- `tests/sim/genome_step_test.cpp` rather than here, because it needs a grid
-- seeded half with one genome and half with another and there is no way to ask
-- for that from the command line: Conway against a rule that cannot reproduce,
-- mutation off entirely, Conway's share of the population going from under three
-- quarters to over nine tenths in four hundred generations.
--
-- B0 cannot work here and the reason is structural rather than an oversight: a
-- cell with no live neighbours has no parent to take a genome from, so it keeps
-- a genome of zero and no birth bit is ever set.
local e = expr

local n = e.count(1)
-- Alive: the survival half, nine bits up. Dead: the birth half.
local which = e.select(e.eq(e.self(), e.int(1)), e.add(n, e.int(9)), n)

return {
    states = 2,
    neighbourhood = { type = "moore", radius = 1 },
    fields = {
        -- No write: the engine owns these bytes at birth and carries them
        -- otherwise. A rule writing its own genome would be Lamarckian.
        { name = "genome", cell_type = "u32" },
    },
    genome = { field = "genome", bits = 18 },
    transition = e.band(e.shr(e.field("genome"), which), e.int(1)),
    metadata = { name = "Lineages" },
}
