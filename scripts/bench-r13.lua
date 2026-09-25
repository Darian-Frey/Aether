-- Benchmark kernel: a Gaussian shell at radius 13. Not a rule worth watching;
-- it exists to put a known number of neighbours through the convolution.
local RADIUS = 13
local profile = {}
for i = 0, RADIUS do
    local x = i / RADIUS
    profile[#profile + 1] = math.exp(-((x - 0.5) ^ 2) / (2 * 0.15 ^ 2))
end
return {
    cell_type = "f32",
    kind = "continuous",
    neighbourhood = { type = "moore", radius = RADIUS },
    kernel = { shape = "radial", profile = profile },
    growth = { form = "polynomial", mu = 0.15, sigma = 0.015, dt = 0.1 },
    metadata = { name = "Benchmark kernel r13" },
}
