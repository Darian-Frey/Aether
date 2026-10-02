#include "rule/lifespan.hpp"

#include "rule/table_layout.hpp"

#include <format>
#include <vector>

namespace aether::rule {

namespace {

// Only a *binary* rule has an unambiguous notion of "alive" to give ages to. A
// rule with three or more states already means something by each of them —
// Wireworld's tail, a cyclic rule's phase — and an age inserted among those
// would be the transform deciding which of the author's states was the living
// one. Refused rather than guessed.
bool spannable(const RuleIR& ir) {
    return ir.cell_type == core::CellType::U8 && ir.states == 2 &&
           (ir.kind == Kind::OuterTotalistic || ir.kind == Kind::CountedTotalistic) &&
           std::holds_alternative<Table>(ir.transition);
}

bool fits(uint16_t states, uint32_t neighbours) {
    const auto size = tableSize(Kind::CountedTotalistic, states, neighbours);
    return size && *size <= kLutMaxEntries;
}

}  // namespace

uint16_t maxLifespan(const RuleIR& base) {
    if (!spannable(base)) return 0;
    const uint32_t N = neighbourCount(base.dimensions, base.neighbourhood);
    uint16_t ages = 1;
    while (ages + 2u <= 256u && fits(static_cast<uint16_t>(ages + 2u), N)) ++ages;
    return ages;
}

std::variant<RuleIR, std::string> applyLifespan(const RuleIR& base, uint16_t ages) {
    if (ages <= 1) return base;
    if (!spannable(base)) {
        return std::string("lifespan applies to two-state table-form rules; this rule has " +
                           std::to_string(base.states) + " states" +
                           (std::holds_alternative<Table>(base.transition) ? "" : " in expression or kernel form"));
    }
    const uint32_t N = neighbourCount(base.dimensions, base.neighbourhood);
    const uint16_t S2 = static_cast<uint16_t>(ages + 1u);   // state 0 plus ages 1..L
    if (S2 > 256) {
        return std::format("lifespan {} would give {} states; the limit is 256 (SPEC §1)", ages, S2);
    }
    const auto size = tableSize(Kind::CountedTotalistic, S2, N);
    if (!size || *size > kLutMaxEntries) {
        return std::format("lifespan {} gives {} states, a table of {} entries against a limit of {}; "
                           "the longest this neighbourhood allows is {}",
                           ages, S2, size ? std::to_string(*size) : "more than 2^64",
                           kLutMaxEntries, maxLifespan(base));
    }

    // Size checked before anything is allocated (AV-010).
    const auto& baseTable = std::get<Table>(base.transition).entries;
    const TableLayout baseLayout(base.kind, 2, N);
    const TableLayout layout(Kind::CountedTotalistic, S2, N);

    // Every age counts as alive, for every own state. That set is the whole of
    // what makes a lifespan work: without it a cell of age three would be
    // invisible to its neighbours and the rule would be a different automaton.
    StateSet live;
    for (uint16_t a = 1; a < S2; ++a) live.set(a);

    Table out;
    out.entries.assign(*size, 0);
    for (uint16_t own = 0; own < S2; ++own) {
        for (uint32_t k = 0; k <= N; ++k) {
            uint8_t next;
            if (own == 0) {
                // A birth: the base's answer for a dead cell, as age 1. A binary
                // outer-totalistic rule indexes as own·(N+1)+k too, which is why
                // one index serves both base kinds (the same fact `decay` uses).
                next = baseTable[baseLayout.indexCounted(0, k)] != 0 ? 1u : 0u;
            } else {
                // A living cell of some age. The base decides whether it is
                // supported at all; the deadline decides whether being supported
                // is enough. The order is not negotiable: a cell at the last age
                // dies *regardless* of its neighbours, which is what makes this a
                // hard lifespan rather than a slower decay.
                const uint8_t supported = baseTable[baseLayout.indexCounted(1, k)];
                next = (supported != 0 && own + 1u < S2) ? static_cast<uint8_t>(own + 1u) : 0u;
            }
            out.entries[layout.indexCounted(static_cast<uint8_t>(own), k)] = next;
        }
    }

    RuleIR ir = base;
    ir.states = S2;
    ir.kind = Kind::CountedTotalistic;
    ir.counted.assign(S2, live);
    ir.transition = std::move(out);
    // The one thing downstream cannot work out for itself: that states 1..L are
    // one cell growing older. The table says every age counts as a neighbour,
    // which is semantics, but not that an age is a poor place to *start* a cell
    // — and the seeding needs exactly that (BUG-026). A hint rather than part of
    // the rule, like `decay_from`, because it changes what a fresh grid is
    // filled with and nothing about how it steps.
    ir.metadata.lifespan = ages;
    return ir;
}

}  // namespace aether::rule
