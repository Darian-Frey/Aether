#include "rule/table_layout.hpp"

#include <limits>

namespace aether::rule {

namespace {

constexpr uint64_t kSaturated = std::numeric_limits<uint64_t>::max();

uint64_t satAdd(uint64_t a, uint64_t b) {
    return (a > kSaturated - b) ? kSaturated : a + b;
}

uint64_t satMul(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    return (a > kSaturated / b) ? kSaturated : a * b;
}

// W(n, m) = C(n + m, m): vectors of m non-negative integers summing to <= n.
// Built by the recurrence W(n, m) = W(n-1, m) + W(n, m-1), which needs only
// saturating addition and so never overflows silently.
std::vector<uint64_t> buildCompositions(uint32_t maxN, uint32_t maxM) {
    const size_t cols = maxM + 1;
    std::vector<uint64_t> w((maxN + 1) * cols, 1);   // W(n,0) = W(0,m) = 1
    for (uint32_t n = 1; n <= maxN; ++n) {
        for (uint32_t m = 1; m <= maxM; ++m) {
            w[n * cols + m] = satAdd(w[(n - 1) * cols + m], w[n * cols + (m - 1)]);
        }
    }
    return w;
}

uint64_t satPow(uint64_t base, uint32_t exp) {
    uint64_t r = 1;
    for (uint32_t i = 0; i < exp; ++i) r = satMul(r, base);
    return r;
}

std::optional<uint64_t> finite(uint64_t v) {
    if (v == kSaturated) return std::nullopt;
    return v;
}

}  // namespace

std::optional<uint64_t> tableSize(Kind kind, uint16_t states, uint32_t neighbours) {
    const uint64_t S = states;
    const uint64_t N = neighbours;
    switch (kind) {
        case Kind::OuterTotalistic: {
            // S * W(N, S-1). Cheap path for the binary case; general path
            // builds the recurrence table.
            if (S == 2) return finite(satMul(S, N + 1));
            const auto w = buildCompositions(neighbours, states - 1);
            return finite(satMul(S, w[neighbours * S + (states - 1)]));
        }
        case Kind::CountedTotalistic:
            // One count, not a vector of them: this is the whole point (D-016).
            return finite(satMul(S, N + 1));
        case Kind::Totalistic:
            return finite(satAdd(satMul(N + 1, S - 1), 1));
        case Kind::NonTotalistic:
            return finite(satMul(S, satPow(S, neighbours)));
        case Kind::Expression:
        case Kind::Continuous:
            return std::nullopt;
    }
    return std::nullopt;
}

TableLayout::TableLayout(Kind kind, uint16_t states, uint32_t neighbours)
    : kind_(kind), states_(states), neighbours_(neighbours),
      size_(tableSize(kind, states, neighbours)) {
    if (kind == Kind::OuterTotalistic) {
        w_ = buildCompositions(neighbours, states - 1);
    }
}

uint64_t TableLayout::compositions(uint32_t n, uint32_t m) const {
    return w_[n * states_ + m];
}

uint64_t TableLayout::indexOuterTotalistic(uint8_t own, std::span<const uint32_t> counts) const {
    // Rank of the count vector among all vectors with sum <= N, lexicographic
    // ascending. For each digit, add the number of vectors that share the
    // prefix and have a smaller value in this position.
    const uint32_t digits = states_ - 1u;
    uint64_t rank      = 0;
    uint32_t remaining = neighbours_;
    for (uint32_t i = 0; i < digits; ++i) {
        const uint32_t after = digits - i - 1;
        for (uint32_t v = 0; v < counts[i]; ++v) {
            rank += compositions(remaining - v, after);
        }
        remaining -= counts[i];
    }
    return static_cast<uint64_t>(own) * compositions(neighbours_, digits) + rank;
}

uint64_t TableLayout::indexTotalistic(uint32_t sum) const {
    return sum;
}

uint64_t TableLayout::indexNonTotalistic(uint8_t own, std::span<const uint8_t> neighbours) const {
    const uint64_t S = states_;
    uint64_t signature = 0;
    uint64_t place     = 1;
    for (size_t i = 0; i < neighbours.size(); ++i) {
        signature += neighbours[i] * place;
        place *= S;
    }
    return static_cast<uint64_t>(own) * place + signature;
}

}  // namespace aether::rule
