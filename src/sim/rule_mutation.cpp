#include "sim/rule_mutation.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace aether::sim {

namespace {

using rule::ExprNode;
using rule::ExprOp;

// SPEC §9.1, Table: a uniformly random index takes a uniformly random
// state other than its current one. Two draws.
void editTable(rule::Table& t, uint16_t states, Pcg32& rng) {
    const uint32_t i = rng.below(static_cast<uint32_t>(t.entries.size()));
    const uint32_t other = rng.below(states - 1u);           // 0..S-2
    const uint8_t cur = t.entries[i];
    t.entries[i] = static_cast<uint8_t>(other >= cur ? other + 1u : other);
}

bool isLiteral(ExprOp op) { return op == ExprOp::IntLiteral || op == ExprOp::FloatLiteral; }

// Operator classes whose members are interchangeable without changing
// arity or type, which is what keeps the tree shape and typing intact.
const std::vector<ExprOp>* operatorClass(ExprOp op) {
    static const std::vector<ExprOp> arithmetic{ExprOp::Add, ExprOp::Sub, ExprOp::Mul, ExprOp::Div, ExprOp::Mod};
    static const std::vector<ExprOp> comparison{ExprOp::Eq, ExprOp::Ne, ExprOp::Lt, ExprOp::Le, ExprOp::Gt, ExprOp::Ge};
    static const std::vector<ExprOp> connective{ExprOp::And, ExprOp::Or};
    for (const auto* c : {&arithmetic, &comparison, &connective}) {
        if (std::find(c->begin(), c->end(), op) != c->end()) return c;
    }
    return nullptr;
}

bool isEditable(ExprOp op) { return isLiteral(op) || operatorClass(op) != nullptr; }

// SPEC §9.1, Expression: a uniformly random editable node; a literal moves
// by ±1 (clamped at zero), an operator becomes another of its class. The
// shape never changes. Two draws.
void editExpression(rule::Expression& e, Pcg32& rng) {
    std::vector<uint32_t> editable;
    for (uint32_t i = 0; i < e.nodes.size(); ++i) {
        if (isEditable(e.nodes[i].op)) editable.push_back(i);
    }
    const uint32_t choice = rng.below(static_cast<uint32_t>(std::max<size_t>(1, editable.size())));
    const uint32_t direction = rng.below(2);
    if (editable.empty()) return;   // nothing to edit; validation will still pass
    ExprNode& n = e.nodes[editable[choice]];
    if (n.op == ExprOp::IntLiteral) {
        n.ival = direction ? n.ival + 1 : std::max<int64_t>(0, n.ival - 1);
    } else if (n.op == ExprOp::FloatLiteral) {
        n.fval = direction ? n.fval + 0.05f : n.fval - 0.05f;
    } else {
        const auto& cls = *operatorClass(n.op);
        const size_t cur = static_cast<size_t>(std::find(cls.begin(), cls.end(), n.op) - cls.begin());
        // "another operator": step by 1..size-1 within the class, using the
        // direction draw as the step parity so the draw count stays fixed.
        const size_t step = cls.size() == 2 ? 1 : (direction ? 1 : cls.size() - 1);
        n.op = cls[(cur + step) % cls.size()];
    }
}

}  // namespace

MutationResult mutateRule(const rule::RuleIR& ir, uint32_t magnitude, Pcg32& streamA) {
    MutationResult result;
    if (std::holds_alternative<rule::Kernel>(ir.transition)) return result;   // Phase 5
    magnitude = std::max(1u, magnitude);

    for (int attempt = 1; attempt <= kMutationAttempts; ++attempt) {
        result.attempts = attempt;
        rule::RuleIR candidate = ir;
        if (auto* t = std::get_if<rule::Table>(&candidate.transition)) {
            for (uint32_t k = 0; k < magnitude; ++k) editTable(*t, ir.states, streamA);
        } else if (auto* e = std::get_if<rule::Expression>(&candidate.transition)) {
            for (uint32_t k = 0; k < magnitude; ++k) editExpression(*e, streamA);
        }
        if (rule::isValid(candidate)) {
            // The notation no longer describes the rule; the name records
            // where it came from.
            const std::string origin = ir.metadata.name.value_or(ir.metadata.source_notation.value_or("rule"));
            candidate.metadata.source_notation.reset();
            candidate.metadata.name = origin.ends_with("*") ? origin : origin + "*";
            result.ir = std::move(candidate);
            return result;
        }
    }
    return result;
}

}  // namespace aether::sim
