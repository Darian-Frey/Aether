-- name: Cyclic CA (14 states)
-- description: Griffeath's cyclic rule with fourteen states: slower spirals, more colour.
-- The rule only ever asks how many neighbours hold the successor state, so
-- it declares that as its counted set. With the full count vector this
-- rule would need 2.8 million entries; counted it needs 126.
local STATES = 14

return {
    states = STATES,
    neighbourhood = { type = "moore", radius = 1 },
    kind = "counted_totalistic",
    metadata = { name = "Cyclic CA (14 states)" },
    counted = function(own) return { (own + 1) % STATES } end,
    transition = function(own, k)
        if k >= 1 then return (own + 1) % STATES end
        return own
    end,
}
