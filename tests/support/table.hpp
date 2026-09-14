// Reading a table entry without caring which indexing scheme the kind uses.
// Tests state what a rule does for a given own state and neighbourhood; the
// layout that expresses it is the compiler's business.

#pragma once

#include "rule/ir.hpp"
#include "rule/table_layout.hpp"

#include <cstdint>
#include <vector>

namespace aether::test {

// `counts[i]` is the number of neighbours in state i+1, as the outer-
// totalistic vector. For a counted rule the entry is found by counting the
// states that own state actually counts.
inline uint8_t tableEntry(const rule::RuleIR& ir, uint8_t own, std::vector<uint32_t> counts) {
    const uint32_t n = rule::neighbourCount(ir.dimensions, ir.neighbourhood);
    const rule::TableLayout layout(ir.kind, ir.states, n);
    counts.resize(ir.states - 1u, 0);
    const auto& entries = std::get<rule::Table>(ir.transition).entries;
    if (ir.kind == rule::Kind::CountedTotalistic) {
        uint32_t k = 0;
        for (uint16_t s = 1; s < ir.states; ++s) {
            if (ir.counted[own].test(s)) k += counts[s - 1u];
        }
        return entries[layout.indexCounted(own, k)];
    }
    return entries[layout.indexOuterTotalistic(own, counts)];
}

}  // namespace aether::test
