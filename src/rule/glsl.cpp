#include "rule/glsl.hpp"

#include <format>
#include <vector>

namespace aether::rule {

namespace {

const char* typeName(ExprType t) {
    switch (t) {
        case ExprType::Int:   return "int";
        case ExprType::Float: return "float";
        case ExprType::Bool:  return "bool";
        case ExprType::Invalid: break;
    }
    return "void";
}

const char* binaryOperator(ExprOp op) {
    switch (op) {
        case ExprOp::Add: return "+";
        case ExprOp::Sub: return "-";
        case ExprOp::Mul: return "*";
        case ExprOp::Eq:  return "==";
        case ExprOp::Ne:  return "!=";
        case ExprOp::Lt:  return "<";
        case ExprOp::Le:  return "<=";
        case ExprOp::Gt:  return ">";
        case ExprOp::Ge:  return ">=";
        case ExprOp::And: return "&&";
        case ExprOp::Or:  return "||";
        default: return nullptr;
    }
}

// Whether any node asks how many neighbours are in a state, which is the
// only reason to build the count array.
bool needsCounts(const Expression& e) {
    for (const ExprNode& n : e.nodes) {
        if (n.op == ExprOp::Count) return true;
    }
    return false;
}

}  // namespace

std::variant<std::string, GlslError> generateGlsl(const RuleIR& ir) {
    if (ir.cell_type != core::CellType::U8) {
        return GlslError{"the codegen backend serves u8 rules only; continuous rules arrive in Phase 5"};
    }
    const auto* expr = std::get_if<Expression>(&ir.transition);
    if (expr == nullptr) {
        return GlslError{std::format("kind {} is not in expression form", toString(ir.kind))};
    }
    if (const auto diagnostics = validate(ir); !diagnostics.empty()) {
        return GlslError{"invalid IR: " + diagnostics.front().message};
    }

    const uint32_t n = neighbourCount(ir.dimensions, ir.neighbourhood);
    const std::vector<ExprType> types = expressionTypes(*expr, n, ir.states);
    if (types.size() != expr->nodes.size() || types.back() != ExprType::Int) {
        return GlslError{"the expression does not produce a state"};
    }

    std::string out;
    out += std::format("uint aether_rule(uint self, uint nbr[{}]) {{\n", n);
    if (needsCounts(*expr)) {
        // Statically bounded, as SPEC §6 requires: the loop runs over the
        // neighbourhood, whose size is fixed at compile time.
        out += std::format("    int cnt[{}];\n", ir.states);
        out += std::format("    for (int i = 0; i < {}; ++i) cnt[i] = 0;\n", ir.states);
        out += std::format("    for (int i = 0; i < {}; ++i) cnt[int(nbr[i])] += 1;\n", n);
    }

    for (size_t i = 0; i < expr->nodes.size(); ++i) {
        const ExprNode& node = expr->nodes[i];
        const std::string t = std::format("t{}", i);
        const std::string a = std::format("t{}", node.a);
        const std::string b = std::format("t{}", node.b);
        const std::string c = std::format("t{}", node.c);
        const char* type = typeName(types[i]);

        switch (node.op) {
            case ExprOp::Self:
                out += std::format("    int {} = int(self);\n", t);
                break;
            case ExprOp::Neighbour:
                out += std::format("    int {} = int(nbr[{}]);\n", t, node.a);
                break;
            case ExprOp::Count:
                out += std::format("    int {} = cnt[{}];\n", t, node.a);
                break;
            case ExprOp::IntLiteral:
                out += std::format("    int {} = {};\n", t, static_cast<int32_t>(node.ival));
                break;
            case ExprOp::FloatLiteral:
                out += std::format("    float {} = {:.9g};\n", t, node.fval);
                break;
            case ExprOp::Div:
                // Division by zero is undefined in GLSL and a trap in C++,
                // so both paths agree to call it zero (SPEC §6).
                out += std::format("    {} {} = ({} == {}) ? {} : {} / {};\n", type, t, b,
                                   types[i] == ExprType::Float ? "0.0" : "0",
                                   types[i] == ExprType::Float ? "0.0" : "0", a, b);
                break;
            case ExprOp::Mod:
                if (types[i] == ExprType::Float) {
                    out += std::format("    float {} = ({} == 0.0) ? 0.0 : mod({}, {});\n", t, b, a, b);
                } else {
                    out += std::format("    int {} = ({} == 0) ? 0 : {} % {};\n", t, b, a, b);
                }
                break;
            case ExprOp::Not:
                out += std::format("    bool {} = !{};\n", t, a);
                break;
            case ExprOp::Select:
                out += std::format("    {} {} = {} ? {} : {};\n", type, t, a, b, c);
                break;
            default: {
                const char* op = binaryOperator(node.op);
                if (op == nullptr) return GlslError{std::format("no code for {}", toString(node.op))};
                out += std::format("    {} {} = {} {} {};\n", type, t, a, op, b);
                break;
            }
        }
    }

    out += std::format("    return uint(clamp(t{}, 0, {}));\n", expr->nodes.size() - 1, ir.states - 1);
    out += "}\n";
    return out;
}

}  // namespace aether::rule
