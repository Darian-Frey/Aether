-- name: Lenia (polynomial)
-- description: A continuous automaton: an annular kernel convolved over float cells, with a polynomial growth band. Structures hold their own size rather than filling or dying.
-- The kernel is a Gaussian shell worked out here, at compile time, and handed
-- over as samples -- nothing in the engine knows what a Gaussian is. The
-- growth function is named rather than written out, because Lenia
-- configurations are published as a form and two numbers (D-020).
--
-- The band is wider than the published orbium numbers use. Those rely on a
-- seed pattern where the convolution varies across a ring; with a band
-- narrower than one time step, anything uniform overshoots it and drains.
-- The orbium seed is a data item for the pattern library (F-027).
local R = 10

local profile = {}
for i = 0, R do
    local x = i / R
    profile[i + 1] = math.exp(-((x - 0.5) ^ 2) / (2 * 0.15 ^ 2))
end

return {
    cell_type = "f32",
    kind = "continuous",
    dimensions = 2,
    neighbourhood = { type = "moore", radius = R },
    kernel = { shape = "radial", profile = profile },
    growth = { form = "polynomial", mu = 0.20, sigma = 0.05, dt = 0.05 },
    metadata = { name = "Lenia (polynomial)" },
}
