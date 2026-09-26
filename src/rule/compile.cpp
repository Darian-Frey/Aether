#include "rule/compile.hpp"

#include "rule/glsl.hpp"

#include <format>

namespace aether::rule {

Backend selectBackend(const RuleIR& ir) {
    // A rule over more than one field cannot be a table: an f32 field has no
    // finite signature to index on, and a table yields one value where such a
    // rule writes several. That is a consequence of the form rather than a
    // tuning choice, so it is not the threshold D-004 puts out of bounds
    // (D-022).
    if (!ir.fields.empty()) return Backend::Codegen;
    if (!std::holds_alternative<Table>(ir.transition)) return Backend::Codegen;
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) return Backend::Codegen;
    return Backend::Lut;
}

namespace {

// The IR's fields with their write expressions typed once, here, so that
// neither stepper infers a type and they cannot disagree about one.
std::vector<CompiledField> compileFields(const RuleIR& ir, uint32_t neighbours) {
    std::vector<CellType> types;
    types.reserve(ir.fields.size());
    for (const Field& f : ir.fields) types.push_back(f.cell_type);

    std::vector<CompiledField> out;
    out.reserve(ir.fields.size());
    for (const Field& f : ir.fields) {
        CompiledField c;
        c.cell_type = f.cell_type;
        if (f.write) {
            c.write = *f.write;
            c.writeTypes = expressionTypes(*f.write, neighbours, ir.states, false, types);
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<CellType> fieldTypesOf(const RuleIR& ir) {
    std::vector<CellType> types;
    types.reserve(ir.fields.size());
    for (const Field& f : ir.fields) types.push_back(f.cell_type);
    return types;
}

}  // namespace

std::variant<CompiledRule, CompileError> compileRule(const RuleIR& ir) {
    // Fields ride on an expression transition and only on one (F-031). A
    // `Kernel` rule declaring a field validates, because the validator types a
    // growth expression's field reads, but nothing yet says what a *neighbour*
    // read means to a rule whose state is a float, and a table cannot express
    // fields at all (D-022 option A). Refused here rather than answered by
    // guesswork; that answer belongs with D-021's multi-kernel question.
    if (!ir.fields.empty() && !std::holds_alternative<Expression>(ir.transition)) {
        return CompileError{"a rule with auxiliary fields must have an expression transition"};
    }

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
            .fields        = {},
            .weights       = std::move(rk.weights),
            .selfWeight    = rk.self,
        };
    }

    if (ir.cell_type != CellType::U8) {
        return CompileError{"both backends serve u8 rules only; continuous rules arrive in Phase 5"};
    }

    // An expression has no finite table, so it goes to codegen (D-004).
    if (const auto* expression = std::get_if<Expression>(&ir.transition)) {
        // A multi-field rule has no GLSL yet: SPEC §6's shape for it is a
        // function per written field, which is F-031's step 3 along with the
        // shader that declares the field samplers. Generating a single
        // function that reads fields would emit identifiers nothing declares,
        // so the text is left empty and `GpuStepper::setRule` refuses the rule
        // outright — a refusal where the rule cannot run rather than a shader
        // that fails to link. The CPU oracle is complete either way.
        std::string generated;
        if (ir.fields.empty()) {
            auto glsl = generateGlsl(ir);
            if (const auto* e = std::get_if<GlslError>(&glsl)) return CompileError{e->message};
            generated = std::get<std::string>(std::move(glsl));
        }
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
            .expressionTypes = expressionTypes(*expression, nbrs, ir.states, false, fieldTypesOf(ir)),
            .glsl          = std::move(generated),
            .fields        = compileFields(ir, nbrs),
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
        .fields        = {},
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
