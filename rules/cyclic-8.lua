-- name: Cyclic CA (8 states)
-- description: Each state is eaten by the next; spirals form out of noise. Griffeath's rule.
-- A loop is the honest way to write this: state s becomes s+1 wherever a
-- neighbour is already there.
local STATES = 8

return {
    states = STATES,
    neighbourhood = { type = "moore", radius = 1 },
    kind = "outer_totalistic",
    metadata = { name = "Cyclic CA (8 states)" },
    transition = function(own, counts)
        local successor = (own + 1) % STATES
        if counts[successor] >= 1 then return successor end
        return own
    end,
}
