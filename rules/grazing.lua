-- name: Grazing
-- description: A two-state rule living off an abiotic resource. Grass regrows toward a patchy soil fertility; plants occupy the ground that can feed them and retreat from the ground that cannot. The Resource panel's regrowth slider is the harshness of the world — it takes the population from a fifth of the grid to nine tenths.
-- palette: 1 = #4CAF50
--
-- The first rule in the library whose population is decided by something other
-- than its own neighbourhood (F-032). The engine regenerates `grass` toward
-- `soil` each generation; this rule only ever draws it down, which is what makes
-- the quantity conserved and the books balanceable (AV-018, D-024).
--
-- The numbers are not arbitrary. A live cell holds its ground exactly where
-- regrowth covers what it eats:
--
--     regen * (soil - grass) = eat     =>     grass settles at soil - eat/regen
--
-- so with a regrowth of 0.02 and an appetite of 0.004 a cell settles 0.2 below
-- its soil, and ground below about 0.35 cannot feed one at all. That is where
-- the pattern comes from: the fertile regions fill and the poor ones stay bare,
-- and the boundary between them is drawn by the noise rather than by the rule.
--
-- Crowded cells eat two and a half times as much, so a patch that fills solid
-- starves itself thin again. Without that term the rule reaches a fixed point
-- within four hundred generations and never moves; with it the boundary is still
-- shifting after two thousand.
--
-- Measured on a 96x96 grid, seeded with `--seed-resource 5:3:0.15:1.0`, at
-- generation 800: regrowth 0.005 holds 22% of the grid, 0.01 holds 36%, 0.02
-- holds 45%, 0.05 holds 75% and 0.1 holds 89%. The slider is the rule.
--
-- It needs seeding before it does anything: a resource nobody has seeded is zero
-- everywhere and every plant starves immediately. Press *Seed the world* in the
-- Resource panel, or pass `--seed-resource` headlessly.
local e = expr

local EAT_ALONE = 0.004   -- settles 0.2 below the soil at a regrowth of 0.02
local EAT_CROWD = 0.020   -- and 1.0 below it, which most ground cannot cover
local CROWD     = 4       -- neighbours before appetite goes up
local FEED      = 0.20    -- grass a cell needs to stay alive
local BIRTH     = 0.45    -- and to be born into

local appetite = e.select(e.ge(e.count(1), e.int(CROWD)),
                          e.float(EAT_CROWD), e.float(EAT_ALONE))

return {
    states = 2,
    neighbourhood = { type = "moore", radius = 1 },
    fields = {
        -- The draw-down, and only the draw-down: regrowth is the engine's.
        { name = "grass", cell_type = "f32",
          write = e.select(e.eq(e.self(), e.int(1)),
                           e.sub(e.field("grass"), appetite),
                           e.field("grass")) },
        -- The world's shape. Nothing writes it, which the validator insists on.
        { name = "soil", cell_type = "f32" },
    },
    resource = { field = "grass", capacity = "soil" },
    transition = e.select(
        e.eq(e.self(), e.int(1)),
        -- Starve below FEED.
        e.select(e.gt(e.field("grass"), e.float(FEED)), e.int(1), e.int(0)),
        -- Spread into well-fed ground next to something already living.
        e.select(e.and_(e.gt(e.field("grass"), e.float(BIRTH)), e.gt(e.count(1), e.int(0))),
                 e.int(1), e.int(0))),
    metadata = { name = "Grazing" },
}
