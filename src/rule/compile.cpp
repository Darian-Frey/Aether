#include "rule/compile.hpp"

#include "rule/glsl.hpp"

#include <format>

namespace aether::rule {

Backend selectBackend(const RuleIR& ir) {
    if (!std::holds_alternative<Table>(ir.transition)) return Backend::Codegen;
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) return Backend::Codegen;
    return Backend::Lut;
}

std::variant<CompiledRule, CompileError> compileRule(const RuleIR& ir) {
    if (const auto ds = validate(ir); !ds.empty()) {
        return CompileError{"invalid IR: " + ds.front().message};
    }
    // A continuous rule is a kernel and a growth function rather than a table,
    // so it is resolved rather than laid out: the profile becomes one weight
    // per offset here, once, and the growth expression is the tree the step
    // walks for the increment.
    if (const auto* kernel = std::get_if<Kernel>(&ir.transition)) {
        auto resolved = resolveKernel(ir);
        if (const auto* e = std::get_if<std::string>(&resolved)) return CompileError{*e};
        auto& rk = std::get<ResolvedKernel>(resolved);
        auto glsl = generateGlsl(ir);
        if (const auto* e = std::get_if<GlslError>(&glsl)) return CompileError{e->message};
        const uint32_t nbrs = neighbourCount(ir.dimensions, ir.neighbourhood);
        return CompiledRule{
            .backend       = Backend::Codegen,
            .ir_hash       = irHash(ir),
            .dimensions    = ir.dimensions,
            .states        = ir.states,
            .kind          = ir.kind,
            .neighbourhood = ir.neighbourhood,
            .counted       = {},
            .boundary      = ir.boundary,
            .offsets       = neighbourOffsets(ir.dimensions, ir.neighbourhood),
            .layout        = TableLayout(ir.kind, ir.states, nbrs),
            .table         = {},
            .aux           = {},
            .expression    = kernel->growth,
            .expressionTypes = expressionTypes(kernel->growth, 0, 0, /*selfIsFloat=*/true),
            .glsl          = std::get<std::string>(std::move(glsl)),
            .weights       = std::move(rk.weights),
            .selfWeight    = rk.self,
        };
    }

    if (ir.cell_type != CellType::U8) {
        return CompileError{"both backends serve u8 rules only; continuous rules arrive in Phase 5"};
    }

    // An expression has no finite table, so it goes to codegen (D-004).
    if (const auto* expression = std::get_if<Expression>(&ir.transition)) {
        auto glsl = generateGlsl(ir);
        if (const auto* e = std::get_if<GlslError>(&glsl)) return CompileError{e->message};
        const uint32_t nbrs = neighbourCount(ir.dimensions, ir.neighbourhood);
        return CompiledRule{
            .backend       = Backend::Codegen,
            .ir_hash       = irHash(ir),
            .dimensions    = ir.dimensions,
            .states        = ir.states,
            .kind          = ir.kind,
            .neighbourhood = ir.neighbourhood,
            .counted       = {},
            .boundary      = ir.boundary,
            .offsets       = neighbourOffsets(ir.dimensions, ir.neighbourhood),
            .layout        = TableLayout(ir.kind, ir.states, nbrs),
            .table         = {},
            .aux           = {},
            .expression    = *expression,
            .expressionTypes = expressionTypes(*expression, nbrs, ir.states),
            .glsl          = std::get<std::string>(std::move(glsl)),
        };
    }

    const auto* table = std::get_if<Table>(&ir.transition);
    if (!table) return CompileError{"the rule has no transition table"};

    // Size before anything is copied or built (AV-010). validate() has
    // already confirmed the table matches it exactly.
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) {
        return CompileError{std::format("table of {} entries exceeds LUT_MAX_ENTRIES ({})",
                                        size ? std::to_string(*size) : "> 2^64", kLutMaxEntries)};
    }

    CompiledRule out{
        .backend       = Backend::Lut,
        .ir_hash       = irHash(ir),
        .dimensions    = ir.dimensions,
        .states        = ir.states,
        .kind          = ir.kind,
        .neighbourhood = ir.neighbourhood,
        .counted       = ir.counted,
        .boundary      = ir.boundary,
        .offsets       = neighbourOffsets(ir.dimensions, ir.neighbourhood),
        .layout        = TableLayout(ir.kind, ir.states, N),
        .table         = table->entries,
        .aux           = {},
        .expression    = {},
        .expressionTypes = {},
        .glsl          = {},
    };

    if (ir.kind == Kind::OuterTotalistic) {
        // Every W value is bounded by the per-state row count, which is
        // <= size / S <= LUT_MAX_ENTRIES, so u32 is safe.
        const uint32_t cols = ir.states;
        out.aux.resize((N + 1u) * cols);
        for (uint32_t n = 0; n <= N; ++n) {
            for (uint32_t m = 0; m < cols; ++m) {
                out.aux[n * cols + m] = static_cast<uint32_t>(out.layout.compositions(n, m));
            }
        }
    } else if (ir.kind == Kind::CountedTotalistic) {
        out.aux.resize(size_t{ir.states} * 8u);
        for (uint16_t own = 0; own < ir.states; ++own) {
            for (uint32_t word = 0; word < 8; ++word) {
                out.aux[size_t{own} * 8u + word] = ir.counted[own].bits[word];
            }
        }
    }
    return out;
}

}  // namespace aether::rule
