#include "rule/decay.hpp"

#include "rule/table_layout.hpp"

#include <format>
#include <vector>

namespace aether::rule {

namespace {

bool decayable(const RuleIR& ir) {
    return ir.cell_type == core::CellType::U8 && ir.kind == Kind::OuterTotalistic &&
           std::holds_alternative<Table>(ir.transition);
}

bool fits(uint16_t states, uint32_t neighbours) {
    const auto size = tableSize(Kind::OuterTotalistic, states, neighbours);
    return size && *size <= kLutMaxEntries;
}

}  // namespace

uint16_t maxDecay(const RuleIR& base) {
    if (!decayable(base)) return 0;
    const uint32_t N = neighbourCount(base.dimensions, base.neighbourhood);
    uint16_t extra = 0;
    while (base.states + extra + 1u <= 256u && fits(static_cast<uint16_t>(base.states + extra + 1u), N)) ++extra;
    return extra;
}

std::variant<RuleIR, std::string> applyDecay(const RuleIR& base, uint16_t extra) {
    if (extra == 0) return base;
    if (!decayable(base)) {
        return std::string("decay applies to table-form outer-totalistic rules; this rule is " +
                           std::string(toString(base.kind)) +
                           (std::holds_alternative<Table>(base.transition) ? "" : " in expression or kernel form"));
    }
    const uint16_t S = base.states;
    if (S + extra > 256) {
        return std::format("decay {} would give {} states; the limit is 256 (SPEC §1)", extra, S + extra);
    }
    const uint32_t N = neighbourCount(base.dimensions, base.neighbourhood);
    const uint16_t S2 = static_cast<uint16_t>(S + extra);
    const auto size = tableSize(Kind::OuterTotalistic, S2, N);
    if (!size || *size > kLutMaxEntries) {
        return std::format("decay {} gives {} states, a table of {} entries against a limit of {}; "
                           "the longest tail this neighbourhood allows is {}",
                           extra, S2, size ? std::to_string(*size) : "more than 2^64", kLutMaxEntries, maxDecay(base));
    }

    // Size checked before anything is allocated (AV-010).
    const TableLayout baseLayout(Kind::OuterTotalistic, S, N);
    const TableLayout layout(Kind::OuterTotalistic, S2, N);
    const auto& baseTable = std::get<Table>(base.transition).entries;

    Table out;
    out.entries.assign(*size, 0);
    std::vector<uint32_t> baseCounts(S - 1u, 0);
    layout.forEachCountVector([&](std::span<const uint32_t> counts) {
        // The rule sees only its own states; tail states count as quiescent,
        // so a fading cell neither feeds a birth nor supports a survival.
        for (uint16_t i = 0; i + 1u < S; ++i) baseCounts[i] = counts[i];
        for (uint16_t own = 0; own < S2; ++own) {
            uint8_t next;
            if (own >= S) {
                next = static_cast<uint8_t>(own + 1u < S2 ? own + 1u : 0u);
            } else {
                const uint8_t plain = baseTable[baseLayout.indexOuterTotalistic(static_cast<uint8_t>(own), baseCounts)];
                next = (plain == 0 && own != 0) ? static_cast<uint8_t>(S) : plain;
            }
            out.entries[layout.indexOuterTotalistic(static_cast<uint8_t>(own), counts)] = next;
        }
    });

    RuleIR ir = base;
    ir.states = S2;
    ir.transition = std::move(out);
    ir.metadata.decay_from = S;
    return ir;
}

}  // namespace aether::rule
