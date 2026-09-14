#include "rule/ir.hpp"

#include "rule/table_layout.hpp"

#include <bit>
#include <cstring>
#include <format>
#include <type_traits>

namespace aether::rule {

// --- Names -------------------------------------------------------------------

std::string_view toString(Boundary v) {
    switch (v) {
        case Boundary::Wrap:   return "wrap";
        case Boundary::Zero:   return "zero";
        case Boundary::Mirror: return "mirror";
    }
    return "?";
}

std::string_view toString(Kind v) {
    switch (v) {
        case Kind::OuterTotalistic: return "outer_totalistic";
        case Kind::CountedTotalistic: return "counted_totalistic";
        case Kind::Totalistic:      return "totalistic";
        case Kind::NonTotalistic:   return "non_totalistic";
        case Kind::Expression:      return "expression";
        case Kind::Continuous:      return "continuous";
    }
    return "?";
}

std::string_view toString(NeighbourhoodType v) {
    switch (v) {
        case NeighbourhoodType::Moore:      return "moore";
        case NeighbourhoodType::VonNeumann: return "von_neumann";
        case NeighbourhoodType::Hexagonal:  return "hexagonal";
    }
    return "?";
}

std::string_view toString(ExprOp v) {
    switch (v) {
        case ExprOp::Self:         return "self";
        case ExprOp::Neighbour:    return "neighbour";
        case ExprOp::Count:        return "count";
        case ExprOp::IntLiteral:   return "int";
        case ExprOp::FloatLiteral: return "float";
        case ExprOp::Add:          return "add";
        case ExprOp::Sub:          return "sub";
        case ExprOp::Mul:          return "mul";
        case ExprOp::Div:          return "div";
        case ExprOp::Mod:          return "mod";
        case ExprOp::Eq:           return "eq";
        case ExprOp::Ne:           return "ne";
        case ExprOp::Lt:           return "lt";
        case ExprOp::Le:           return "le";
        case ExprOp::Gt:           return "gt";
        case ExprOp::Ge:           return "ge";
        case ExprOp::And:          return "and";
        case ExprOp::Or:           return "or";
        case ExprOp::Not:          return "not";
        case ExprOp::Select:       return "select";
    }
    return "?";
}

std::optional<Boundary> parseBoundary(std::string_view s) {
    if (s == "wrap")   return Boundary::Wrap;
    if (s == "zero")   return Boundary::Zero;
    if (s == "mirror") return Boundary::Mirror;
    return std::nullopt;
}

std::optional<Kind> parseKind(std::string_view s) {
    if (s == "outer_totalistic") return Kind::OuterTotalistic;
    if (s == "counted_totalistic") return Kind::CountedTotalistic;
    if (s == "totalistic")       return Kind::Totalistic;
    if (s == "non_totalistic")   return Kind::NonTotalistic;
    if (s == "expression")       return Kind::Expression;
    if (s == "continuous")       return Kind::Continuous;
    return std::nullopt;
}

std::optional<NeighbourhoodType> parseNeighbourhoodType(std::string_view s) {
    if (s == "moore")       return NeighbourhoodType::Moore;
    if (s == "von_neumann") return NeighbourhoodType::VonNeumann;
    if (s == "hexagonal" || s == "hex") return NeighbourhoodType::Hexagonal;
    return std::nullopt;
}

// --- Expression typing -------------------------------------------------------

namespace {

enum class ExprType : uint8_t { Int, Float, Bool, Invalid };

struct ExprContext {
    uint32_t neighbours;   // valid Neighbour indices are < this
    uint16_t states;       // valid Count arguments are < this
};

int arity(ExprOp op) {
    switch (op) {
        case ExprOp::Self: case ExprOp::Neighbour: case ExprOp::Count:
        case ExprOp::IntLiteral: case ExprOp::FloatLiteral:
            return 0;
        case ExprOp::Not:
            return 1;
        case ExprOp::Select:
            return 3;
        default:
            return 2;
    }
}

// Type-checks the arena, appending diagnostics under `where`. Returns the
// root type, or Invalid if anything failed. Children must precede parents;
// that rule is what makes the tree acyclic, so it is checked here too.
ExprType checkExpression(const Expression& e, const ExprContext& ctx,
                         std::string_view where, std::vector<Diagnostic>& out) {
    if (e.nodes.empty()) {
        out.push_back({std::format("{}: expression has no nodes", where)});
        return ExprType::Invalid;
    }

    std::vector<ExprType> types(e.nodes.size(), ExprType::Invalid);
    bool ok = true;

    auto fail = [&](size_t i, std::string msg) {
        out.push_back({std::format("{}: node {} ({}): {}", where, i,
                                   toString(e.nodes[i].op), msg)});
        ok = false;
    };

    for (size_t i = 0; i < e.nodes.size(); ++i) {
        const ExprNode& n = e.nodes[i];
        const int k = arity(n.op);
        const uint32_t kids[3] = {n.a, n.b, n.c};
        bool kidsOk = true;
        for (int j = 0; j < k; ++j) {
            if (kids[j] >= i) {
                fail(i, std::format("child {} refers forward to node {}", j, kids[j]));
                kidsOk = false;
            }
        }
        if (!kidsOk) continue;

        const ExprType ta = k > 0 ? types[n.a] : ExprType::Invalid;
        const ExprType tb = k > 1 ? types[n.b] : ExprType::Invalid;
        const ExprType tc = k > 2 ? types[n.c] : ExprType::Invalid;
        auto numeric = [](ExprType t) { return t == ExprType::Int || t == ExprType::Float; };

        switch (n.op) {
            case ExprOp::Self:
                types[i] = ExprType::Int;
                break;
            case ExprOp::Neighbour:
                if (n.a >= ctx.neighbours) {
                    fail(i, std::format("neighbour index {} out of range (N = {})", n.a, ctx.neighbours));
                } else {
                    types[i] = ExprType::Int;
                }
                break;
            case ExprOp::Count:
                if (n.a >= ctx.states) {
                    fail(i, std::format("count of state {} out of range (S = {})", n.a, ctx.states));
                } else {
                    types[i] = ExprType::Int;
                }
                break;
            case ExprOp::IntLiteral:
                types[i] = ExprType::Int;
                break;
            case ExprOp::FloatLiteral:
                types[i] = ExprType::Float;
                break;
            case ExprOp::Add: case ExprOp::Sub: case ExprOp::Mul:
            case ExprOp::Div: case ExprOp::Mod:
                if (!numeric(ta) || ta != tb) fail(i, "operands must be numeric and of the same type");
                else types[i] = ta;
                break;
            case ExprOp::Eq: case ExprOp::Ne: case ExprOp::Lt:
            case ExprOp::Le: case ExprOp::Gt: case ExprOp::Ge:
                if (!numeric(ta) || ta != tb) fail(i, "operands must be numeric and of the same type");
                else types[i] = ExprType::Bool;
                break;
            case ExprOp::And: case ExprOp::Or:
                if (ta != ExprType::Bool || tb != ExprType::Bool) fail(i, "operands must be boolean");
                else types[i] = ExprType::Bool;
                break;
            case ExprOp::Not:
                if (ta != ExprType::Bool) fail(i, "operand must be boolean");
                else types[i] = ExprType::Bool;
                break;
            case ExprOp::Select:
                if (ta != ExprType::Bool) fail(i, "condition must be boolean");
                else if (tb == ExprType::Invalid || tb != tc) fail(i, "branches must have the same type");
                else types[i] = tb;
                break;
        }
    }

    // Every node except the root must be reachable from the root; an orphan
    // is not harmful to evaluate but is a sign the arena was built wrongly,
    // and it would distort node-uniform mutation (SPEC §9.1).
    std::vector<bool> reached(e.nodes.size(), false);
    reached.back() = true;
    for (size_t i = e.nodes.size(); i-- > 0;) {
        if (!reached[i]) {
            fail(i, "unreachable from the root");
            continue;
        }
        const int k = arity(e.nodes[i].op);
        const uint32_t kids[3] = {e.nodes[i].a, e.nodes[i].b, e.nodes[i].c};
        for (int j = 0; j < k; ++j) reached[kids[j]] = true;
    }

    return ok ? types.back() : ExprType::Invalid;
}

// Integer literals that can be the expression's result directly (the root, or
// a branch of a Select that is) must be valid state indices (SPEC §4 rule 2).
void checkResultLiterals(const Expression& e, uint16_t states,
                         std::string_view where, std::vector<Diagnostic>& out) {
    std::vector<uint32_t> stack{static_cast<uint32_t>(e.nodes.size() - 1)};
    while (!stack.empty()) {
        const uint32_t i = stack.back();
        stack.pop_back();
        const ExprNode& n = e.nodes[i];
        if (n.op == ExprOp::Select) {
            stack.push_back(n.b);
            stack.push_back(n.c);
        } else if (n.op == ExprOp::IntLiteral) {
            if (n.ival < 0 || n.ival >= states) {
                out.push_back({std::format("{}: node {}: result literal {} outside 0..{}",
                                           where, i, n.ival, states - 1)});
            }
        }
    }
}

}  // namespace

// --- Validation --------------------------------------------------------------

std::vector<Diagnostic> validate(const RuleIR& ir) {
    std::vector<Diagnostic> out;
    auto err = [&](std::string msg) { out.push_back({std::move(msg)}); };

    if (ir.ir_version != kIrVersion) {
        err(std::format("ir_version {} is not the supported version {}", ir.ir_version, kIrVersion));
    }
    if (ir.dimensions < 1 || ir.dimensions > 3) {
        err(std::format("dimensions must be 1, 2 or 3 (got {})", ir.dimensions));
        return out;   // nothing below is meaningful
    }
    if (ir.neighbourhood.radius < 1) {
        err("neighbourhood radius must be >= 1");
        return out;
    }
    if (ir.neighbourhood.type == NeighbourhoodType::Hexagonal && ir.dimensions == 3) {
        err("hexagonal neighbourhoods are defined for 1D and 2D lattices only");
        return out;
    }

    const bool continuous = ir.cell_type == CellType::F32;
    if (!continuous && (ir.states < 2 || ir.states > 256)) {
        err(std::format("states must be in 2..256 (got {})", ir.states));
    }

    // Rule 5: f32 <=> continuous, and the transition form must match.
    if (continuous != (ir.kind == Kind::Continuous)) {
        err(std::format("cell_type {} does not agree with kind {}",
                        toString(ir.cell_type), toString(ir.kind)));
    }
    const bool formOk = [&] {
        switch (ir.kind) {
            case Kind::OuterTotalistic:
            case Kind::CountedTotalistic:
            case Kind::Totalistic:
                return std::holds_alternative<Table>(ir.transition);
            case Kind::NonTotalistic:
                return std::holds_alternative<Table>(ir.transition) ||
                       std::holds_alternative<Expression>(ir.transition);
            case Kind::Expression:
                return std::holds_alternative<Expression>(ir.transition);
            case Kind::Continuous:
                return std::holds_alternative<Kernel>(ir.transition);
        }
        return false;
    }();
    if (!formOk) {
        err(std::format("kind {} does not permit this transition form", toString(ir.kind)));
    }
    // The counted sets are part of the rule's meaning, so they are checked
    // as strictly as the table (D-016).
    if (ir.kind == Kind::CountedTotalistic) {
        if (ir.counted.size() != ir.states) {
            err(std::format("counted_totalistic needs one counted set per state: {} sets for {} states",
                            ir.counted.size(), ir.states));
        } else {
            for (size_t own = 0; own < ir.counted.size(); ++own) {
                for (uint32_t s = ir.states; s < 256; ++s) {
                    if (ir.counted[own].test(static_cast<uint16_t>(s))) {
                        err(std::format("counted set for state {} includes state {}, outside 0..{}",
                                        own, s, ir.states - 1));
                        break;
                    }
                }
            }
        }
    } else if (!ir.counted.empty()) {
        err(std::format("kind {} must not carry counted sets", toString(ir.kind)));
    }
    if (!out.empty()) return out;

    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);

    // Rule 4: signature must fit a u64.
    if (ir.kind == Kind::NonTotalistic && N > 64) {
        err(std::format("non-totalistic neighbourhood has {} neighbours; the limit is 64", N));
    }

    if (const auto* table = std::get_if<Table>(&ir.transition)) {
        // Rule 3: exact size.
        const auto expected = tableSize(ir.kind, ir.states, N);
        if (!expected) {
            err(std::format("table size for kind {} with S={} N={} does not fit in 64 bits",
                            toString(ir.kind), ir.states, N));
        } else if (table->entries.size() != *expected) {
            err(std::format("table has {} entries; kind {} with S={} N={} requires {}",
                            table->entries.size(), toString(ir.kind), ir.states, N, *expected));
        }
        // Rule 2: every entry is a state.
        for (size_t i = 0; i < table->entries.size(); ++i) {
            if (table->entries[i] >= ir.states) {
                err(std::format("table entry {} is state {}, outside 0..{}",
                                i, table->entries[i], ir.states - 1));
                break;   // one is enough; a mutated table will not have thousands
            }
        }
    } else if (const auto* expr = std::get_if<Expression>(&ir.transition)) {
        // Rule 6.
        const ExprType t = checkExpression(*expr, {N, ir.states}, "transition", out);
        if (t != ExprType::Invalid && t != ExprType::Int) {
            err("transition expression must produce an integer state");
        }
        if (t == ExprType::Int) checkResultLiterals(*expr, ir.states, "transition", out);
    } else if (const auto* kernel = std::get_if<Kernel>(&ir.transition)) {
        if (kernel->profile.empty()) {
            err("kernel has no coefficients");
        }
        if (kernel->shape == Kernel::Shape::Explicit) {
            const uint32_t side = 2u * ir.neighbourhood.radius + 1u;
            uint64_t expected = 1;
            for (int d = 0; d < ir.dimensions; ++d) expected *= side;
            if (kernel->profile.size() != expected) {
                err(std::format("explicit kernel has {} weights; radius {} in {}D needs {}",
                                kernel->profile.size(), ir.neighbourhood.radius,
                                ir.dimensions, expected));
            }
        }
        // The growth function sees only the convolution result (Self).
        const ExprType t = checkExpression(kernel->growth, {0, 0}, "growth", out);
        if (t != ExprType::Invalid && t != ExprType::Float) {
            err("growth expression must produce a float");
        }
    }

    return out;
}

bool isValid(const RuleIR& ir) {
    return validate(ir).empty();
}

// --- Hash --------------------------------------------------------------------

namespace {

// FNV-1a over a canonical little-endian byte stream. Not cryptographic; it
// only needs to be stable and well distributed.
class Hasher {
public:
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h_ ^= b[i];
            h_ *= 0x100000001b3ULL;
        }
    }
    template <typename T>
    void integer(T v) {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        auto u = static_cast<uint64_t>(v);
        unsigned char le[8];
        for (int i = 0; i < 8; ++i) le[i] = static_cast<unsigned char>((u >> (8 * i)) & 0xff);
        bytes(le, sizeof(T) > 8 ? 8 : sizeof(T));
    }
    void real(float v) { integer(std::bit_cast<uint32_t>(v)); }
    uint64_t value() const { return h_; }

private:
    uint64_t h_ = 0xcbf29ce484222325ULL;
};

void hashExpression(Hasher& h, const Expression& e) {
    h.integer(static_cast<uint32_t>(e.nodes.size()));
    for (const ExprNode& n : e.nodes) {
        h.integer(n.op);
        h.integer(n.a);
        h.integer(n.b);
        h.integer(n.c);
        h.integer(n.ival);
        h.real(n.fval);
    }
}

}  // namespace

uint64_t irHash(const RuleIR& ir) {
    Hasher h;
    h.integer(ir.ir_version);
    h.integer(ir.dimensions);
    h.integer(ir.cell_type);
    h.integer(ir.states);
    h.integer(ir.neighbourhood.type);
    h.integer(ir.neighbourhood.radius);
    h.integer(ir.boundary);
    h.integer(ir.kind);
    // Only rules that have counted sets hash them, so adding the field left
    // every rule that existed before it hashing exactly as it did (D-016).
    if (!ir.counted.empty()) {
        h.integer(static_cast<uint32_t>(ir.counted.size()));
        for (const StateSet& set : ir.counted) {
            for (uint32_t word : set.bits) h.integer(word);
        }
    }
    h.integer(static_cast<uint8_t>(ir.transition.index()));
    if (const auto* t = std::get_if<Table>(&ir.transition)) {
        h.integer(static_cast<uint64_t>(t->entries.size()));
        h.bytes(t->entries.data(), t->entries.size());
    } else if (const auto* e = std::get_if<Expression>(&ir.transition)) {
        hashExpression(h, *e);
    } else if (const auto* k = std::get_if<Kernel>(&ir.transition)) {
        h.integer(k->shape);
        h.integer(static_cast<uint32_t>(k->profile.size()));
        for (float f : k->profile) h.real(f);
        hashExpression(h, k->growth);
    }
    return h.value();
}

}  // namespace aether::rule
