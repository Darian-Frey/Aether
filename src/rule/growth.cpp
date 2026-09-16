#include "rule/growth.hpp"

#include <format>

namespace aether::rule {

std::string_view toString(GrowthForm f) {
    switch (f) {
        case GrowthForm::Rectangular: return "rectangular";
        case GrowthForm::Polynomial:  return "polynomial";
    }
    return "?";
}

std::optional<GrowthForm> parseGrowthForm(std::string_view s) {
    if (s == "rectangular") return GrowthForm::Rectangular;
    if (s == "polynomial")  return GrowthForm::Polynomial;
    return std::nullopt;
}

std::vector<std::string> problems(const GrowthSpec& g) {
    std::vector<std::string> out;
    if (!(g.sigma > 0.0f)) {
        out.push_back(std::format("growth sigma must be positive (got {})", g.sigma));
    }
    if (!(g.mu >= 0.0f) || !(g.mu <= 1.0f)) {
        // The convolution of a normalised kernel over cells in [0, 1] lands in
        // [0, 1], so a peak outside it can never be reached.
        out.push_back(std::format("growth mu must be in 0..1 (got {})", g.mu));
    }
    return out;
}

namespace {

// Small arena builder. Children are appended before their parents, which is
// the ordering the evaluator and the generator both rely on.
class Arena {
public:
    uint32_t op(ExprOp o, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
        nodes_.push_back({o, a, b, c, 0, 0.0f});
        return last();
    }
    uint32_t lit(float v) {
        nodes_.push_back({ExprOp::FloatLiteral, 0, 0, 0, 0, v});
        return last();
    }
    Expression take() { return Expression{std::move(nodes_)}; }

private:
    uint32_t last() const { return static_cast<uint32_t>(nodes_.size() - 1); }
    std::vector<ExprNode> nodes_;
};

}  // namespace

Expression growthExpression(const GrowthSpec& g) {
    Arena a;
    const uint32_t u = a.op(ExprOp::Self);   // the convolution result (BUG-010)

    if (g.form == GrowthForm::Rectangular) {
        const uint32_t lo    = a.lit(g.mu - g.sigma);
        const uint32_t hi    = a.lit(g.mu + g.sigma);
        const uint32_t above = a.op(ExprOp::Ge, u, lo);
        const uint32_t below = a.op(ExprOp::Le, u, hi);
        const uint32_t in    = a.op(ExprOp::And, above, below);
        const uint32_t one   = a.lit(1.0f);
        const uint32_t minus = a.lit(-1.0f);
        a.op(ExprOp::Select, in, one, minus);
        return a.take();
    }

    // 2 * max(0, 1 - (u - mu)^2 / (9 sigma^2))^4 - 1.
    const uint32_t mu     = a.lit(g.mu);
    const uint32_t d      = a.op(ExprOp::Sub, u, mu);
    const uint32_t dd     = a.op(ExprOp::Mul, d, d);
    const uint32_t spread = a.lit(9.0f * g.sigma * g.sigma);
    const uint32_t ratio  = a.op(ExprOp::Div, dd, spread);
    const uint32_t one    = a.lit(1.0f);
    const uint32_t q      = a.op(ExprOp::Sub, one, ratio);
    const uint32_t zero   = a.lit(0.0f);
    const uint32_t pos    = a.op(ExprOp::Gt, q, zero);
    const uint32_t clamped = a.op(ExprOp::Select, pos, q, zero);   // max(q, 0)
    const uint32_t q2     = a.op(ExprOp::Mul, clamped, clamped);
    const uint32_t q4     = a.op(ExprOp::Mul, q2, q2);
    const uint32_t two    = a.lit(2.0f);
    const uint32_t scaled = a.op(ExprOp::Mul, two, q4);
    const uint32_t one2   = a.lit(1.0f);
    a.op(ExprOp::Sub, scaled, one2);
    return a.take();
}

}  // namespace aether::rule
