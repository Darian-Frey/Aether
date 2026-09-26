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

// Emits one statement per node, in arena order. Shared by the two generated
// functions: a transition expression, where Self is the own state, and a
// growth expression, where Self is the convolution result (BUG-010).
std::variant<std::string, GlslError> emitNodes(const Expression& expr, const std::vector<ExprType>& types,
                                               bool selfIsFloat) {
    std::string out;
    // Every float temporary is `precise`, which forbids the compiler
    // contracting a*b+c into an fma and reassociating. Without it the shader
    // computes a *better* answer than the oracle and the two paths disagree,
    // which is worse than both being slightly wrong (SPEC §6, AV-007, AV-015).
    // It used to be applied only inside a growth expression, on the grounds
    // that nothing else produced a float. An `f32` auxiliary field does, in an
    // otherwise integer transition, so the qualifier follows the type rather
    // than the surrounding function (F-031).
    auto decl = [&](ExprType t) {
        return std::string(t == ExprType::Float ? "precise " : "") + typeName(t);
    };
    // Flush-to-zero on subnormals, after every float operation. GLSL does not
    // require an implementation to support values below FLT_MIN and both GPUs
    // here flush them; C++ does not, so a decaying float expression diverged
    // from the oracle at the 120th generation (BUG-021, SPEC §6). Doing it
    // explicitly here holds in both directions: a driver that flushes finds the
    // value already zero, one that does not gets the zero the oracle produced,
    // and no input can be subnormal by the time it is read.
    auto flush = [&](ExprType t, const std::string& name) {
        return t == ExprType::Float ? glslFlushStatement(name) : std::string{};
    };
    for (size_t i = 0; i < expr.nodes.size(); ++i) {
        const ExprNode& node = expr.nodes[i];
        const std::string t = std::format("t{}", i);
        const std::string a = std::format("t{}", node.a);
        const std::string b = std::format("t{}", node.b);
        const std::string c = std::format("t{}", node.c);

        // Every arm below declares `t` exactly once, so the flush is appended
        // once after the switch rather than in each of the fifteen arms.
        const auto emitted = out.size();
        switch (node.op) {
            case ExprOp::Self:
                if (selfIsFloat) out += std::format("    {} {} = conv;\n", decl(ExprType::Float), t);
                else             out += std::format("    int {} = int(self);\n", t);
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
                out += std::format("    {} {} = {:.9g};\n", decl(ExprType::Float), t, node.fval);
                break;
            case ExprOp::Div:
                // Division by zero is undefined in GLSL and a trap in C++,
                // so both paths agree to call it zero (SPEC §6).
                out += std::format("    {} {} = ({} == {}) ? {} : {} / {};\n", decl(types[i]), t, b,
                                   types[i] == ExprType::Float ? "0.0" : "0",
                                   types[i] == ExprType::Float ? "0.0" : "0", a, b);
                break;
            case ExprOp::Mod:
                if (types[i] == ExprType::Float) {
                    out += std::format("    {} {} = ({} == 0.0) ? 0.0 : mod({}, {});\n", decl(ExprType::Float), t, b, a, b);
                } else {
                    out += std::format("    int {} = ({} == 0) ? 0 : {} % {};\n", t, b, a, b);
                }
                break;
            case ExprOp::FieldSelf:
                out += std::format("    {} {} = fld.{};\n", decl(types[i]), t,
                                   glslFieldSelfMember(node.a));
                break;
            case ExprOp::FieldNeighbour:
                out += std::format("    {} {} = fld.{}[{}];\n", decl(types[i]), t,
                                   glslFieldNbrMember(node.a), node.b);
                break;
            case ExprOp::Not:
                out += std::format("    bool {} = !{};\n", t, a);
                break;
            case ExprOp::Select:
                out += std::format("    {} {} = {} ? {} : {};\n", decl(types[i]), t, a, b, c);
                break;
            default: {
                const char* op = binaryOperator(node.op);
                if (op == nullptr) return GlslError{std::format("no code for {}", toString(node.op))};
                out += std::format("    {} {} = {} {} {};\n", decl(types[i]), t, a, op, b);
                break;
            }
        }
        if (out.size() != emitted) out += flush(types[i], t);
    }

    return out;
}

// The struct every function of a multi-field rule takes, and the only place
// its shape is decided. One member pair per declared field, typed from the
// field's cell type, so a read costs no conversion at either end (F-031).
std::string fieldsStructDefinition(const RuleIR& ir, uint32_t neighbours) {
    std::string out = std::format("struct {} {{\n", glslFieldsStruct());
    for (size_t f = 0; f < ir.fields.size(); ++f) {
        const std::string type = glslFieldType(ir.fields[f].cell_type);
        out += std::format("    {} {};\n", type, glslFieldSelfMember(f));
        out += std::format("    {} {}[{}];\n", type, glslFieldNbrMember(f), neighbours);
    }
    out += "};\n";
    return out;
}

}  // namespace

std::string glslFieldsStruct() { return "AetherFields"; }
std::string glslFieldSelfMember(size_t index) { return std::format("f{}_self", index); }
std::string glslFieldNbrMember(size_t index) { return std::format("f{}_nbr", index); }
std::string glslFieldFunction(size_t index) { return std::format("aether_field_{}", index); }

std::string glslFieldType(CellType type) {
    return type == CellType::F32 ? "float" : "int";
}

// Written out in full rather than as 1.175494e-38, so that what the compiler
// rounds it to is unambiguously 2^-126 and not the float below it.
std::string glslSubnormalMin() { return "1.17549435082228751e-38"; }

std::string glslFlushStatement(std::string_view variable) {
    return std::format("    {0} = (abs({0}) < {1}) ? 0.0 : {0};\n", variable, glslSubnormalMin());
}

std::variant<std::string, GlslError> generateGlsl(const RuleIR& ir) {
    if (const auto diagnostics = validate(ir); !diagnostics.empty()) {
        return GlslError{"invalid IR: " + diagnostics.front().message};
    }

    // A continuous rule generates the other function of SPEC §6: the growth
    // expression turned into an increment, applied to the cell and clamped.
    // The convolution itself is the shader's, not the generator's.
    if (const auto* kernel = std::get_if<Kernel>(&ir.transition)) {
        const std::vector<ExprType> types = expressionTypes(kernel->growth, 0, 0, /*selfIsFloat=*/true);
        if (types.size() != kernel->growth.nodes.size() || types.back() != ExprType::Float) {
            return GlslError{"the growth expression does not produce a float"};
        }
        auto body = emitNodes(kernel->growth, types, true);
        if (const auto* e = std::get_if<GlslError>(&body)) return *e;
        std::string out = "float aether_rule_f(float self, float conv) {\n";
        out += std::get<std::string>(body);
        // `self + increment` is one more float operation and gets the same
        // flush as every other: it is where a decaying field lands closest to
        // zero, since the increment that cancels the value is the one that
        // brings it there (BUG-021).
        out += std::format("    precise float raw = self + t{};\n", kernel->growth.nodes.size() - 1);
        out += glslFlushStatement("raw");
        out += "    return clamp(raw, 0.0, 1.0);\n";
        out += "}\n";
        return out;
    }

    if (ir.cell_type != core::CellType::U8) {
        return GlslError{"the codegen backend serves u8 rules and kernels"};
    }
    const auto* expr = std::get_if<Expression>(&ir.transition);
    if (expr == nullptr) {
        return GlslError{std::format("kind {} is not in expression form", toString(ir.kind))};
    }

    const uint32_t n = neighbourCount(ir.dimensions, ir.neighbourhood);
    std::vector<CellType> fieldTypes;
    fieldTypes.reserve(ir.fields.size());
    for (const Field& f : ir.fields) fieldTypes.push_back(f.cell_type);

    const std::vector<ExprType> types = expressionTypes(*expr, n, ir.states, false, fieldTypes);
    if (types.size() != expr->nodes.size() || types.back() != ExprType::Int) {
        return GlslError{"the expression does not produce a state"};
    }

    // A rule with no fields generates exactly what it always generated: no
    // struct, no extra parameter, byte for byte. That is what keeps this
    // additive (F-031).
    const bool hasFields = !ir.fields.empty();
    const std::string params = hasFields
                                   ? std::format("uint self, uint nbr[{}], {} fld", n, glslFieldsStruct())
                                   : std::format("uint self, uint nbr[{}]", n);

    // The counts array is emitted per function, and only where that function
    // asks for a count: a field write that counts neighbours needs its own,
    // and one that does not should not pay for it.
    auto emitCounts = [&](const Expression& e) {
        std::string out;
        if (!needsCounts(e)) return out;
        // Statically bounded, as SPEC §6 requires: the loop runs over the
        // neighbourhood, whose size is fixed at compile time.
        out += std::format("    int cnt[{}];\n", ir.states);
        out += std::format("    for (int i = 0; i < {}; ++i) cnt[i] = 0;\n", ir.states);
        out += std::format("    for (int i = 0; i < {}; ++i) cnt[int(nbr[i])] += 1;\n", n);
        return out;
    };

    std::string out;
    if (hasFields) out += fieldsStructDefinition(ir, n);

    out += std::format("uint aether_rule({}) {{\n", params);
    out += emitCounts(*expr);
    auto body = emitNodes(*expr, types, /*selfIsFloat=*/false);
    if (const auto* e = std::get_if<GlslError>(&body)) return *e;
    out += std::get<std::string>(body);
    out += std::format("    return uint(clamp(t{}, 0, {}));\n", expr->nodes.size() - 1, ir.states - 1);
    out += "}\n";

    // One function per *written* field. A field the rule leaves alone gets
    // none: carrying its value forward is a copy, and the copy belongs to
    // whoever owns the storage rather than to a generated function that would
    // only return its argument.
    for (size_t f = 0; f < ir.fields.size(); ++f) {
        const Field& field = ir.fields[f];
        if (!field.write) continue;
        const std::vector<ExprType> wtypes =
            expressionTypes(*field.write, n, ir.states, false, fieldTypes);
        const ExprType want = field.cell_type == CellType::F32 ? ExprType::Float : ExprType::Int;
        if (wtypes.size() != field.write->nodes.size() || wtypes.back() != want) {
            return GlslError{std::format("field '{}' does not produce {}", field.name,
                                         want == ExprType::Float ? "a float" : "an integer")};
        }
        auto wbody = emitNodes(*field.write, wtypes, /*selfIsFloat=*/false);
        if (const auto* e = std::get_if<GlslError>(&wbody)) return *e;

        out += std::format("{} {}({}) {{\n", glslFieldType(field.cell_type), glslFieldFunction(f), params);
        out += emitCounts(*field.write);
        out += std::get<std::string>(wbody);
        const std::string root = std::format("t{}", field.write->nodes.size() - 1);
        // The u8 clamp is the width of the storage, not a state range, and an
        // f32 field is written as computed. The twin of evalFieldWrite in
        // sim/cpu_step.cpp (SPEC §6).
        if (field.cell_type == CellType::F32) out += std::format("    return {};\n", root);
        else                                  out += std::format("    return clamp({}, 0, 255);\n", root);
        out += "}\n";
    }
    return out;
}

}  // namespace aether::rule
