#include "rule/lut.hpp"

#include <format>

namespace aether::rule {

Backend selectBackend(const RuleIR& ir) {
    if (!std::holds_alternative<Table>(ir.transition)) return Backend::Codegen;
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) return Backend::Codegen;
    return Backend::Lut;
}

std::variant<LutRule, CompileError> compileLut(const RuleIR& ir) {
    if (const auto ds = validate(ir); !ds.empty()) {
        return CompileError{"invalid IR: " + ds.front().message};
    }
    if (ir.cell_type != CellType::U8) {
        return CompileError{"the table backend serves u8 rules only"};
    }
    const auto* table = std::get_if<Table>(&ir.transition);
    if (!table) {
        return CompileError{std::format("kind {} in {} form has no table",
                                        toString(ir.kind),
                                        std::holds_alternative<Expression>(ir.transition) ? "expression" : "kernel")};
    }

    // Size before anything is copied or built (AV-010). validate() has
    // already confirmed the table matches it exactly.
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) {
        return CompileError{std::format("table of {} entries exceeds LUT_MAX_ENTRIES ({})",
                                        size ? std::to_string(*size) : "> 2^64", kLutMaxEntries)};
    }

    LutRule out{
        .ir_hash       = irHash(ir),
        .dimensions    = ir.dimensions,
        .states        = ir.states,
        .kind          = ir.kind,
        .neighbourhood = ir.neighbourhood,
        .boundary      = ir.boundary,
        .offsets       = neighbourOffsets(ir.dimensions, ir.neighbourhood),
        .layout        = TableLayout(ir.kind, ir.states, N),
        .table         = table->entries,
        .w             = {},
    };

    if (ir.kind == Kind::OuterTotalistic) {
        // Every W value is bounded by the per-state row count, which is
        // <= size / S <= LUT_MAX_ENTRIES, so u32 is safe.
        const uint32_t cols = ir.states;
        out.w.resize((N + 1u) * cols);
        for (uint32_t n = 0; n <= N; ++n) {
            for (uint32_t m = 0; m < cols; ++m) {
                out.w[n * cols + m] = static_cast<uint32_t>(out.layout.compositions(n, m));
            }
        }
    }
    return out;
}

}  // namespace aether::rule
