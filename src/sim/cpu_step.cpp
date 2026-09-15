#include "sim/cpu_step.hpp"

#include "sim/boundary.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aether::sim {

using rule::Kind;

namespace {

// Wrapping 32-bit arithmetic, so the oracle overflows exactly where GLSL
// does rather than wandering into undefined behaviour (SPEC §6).
int32_t wrapAdd(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t wrapSub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t wrapMul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

struct Value {
    int32_t i = 0;
    float   f = 0.0f;
    bool    b = false;
};

// Walks the arena in order, which is valid because every child precedes its
// parent. The twin of rule/glsl.cpp: the two must agree on every rule, and
// the backend equivalence test is what says they do (AV-007).
uint8_t evalExpression(const rule::CompiledRule& rule, uint8_t own, std::span<const uint8_t> nbr,
                       std::span<const uint32_t> counts, std::vector<Value>& scratch) {
    const auto& nodes = rule.expression.nodes;
    const auto& types = rule.expressionTypes;
    scratch.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const rule::ExprNode& node = nodes[i];
        const Value& a = scratch[node.a];
        const Value& b = scratch[node.b];
        const Value& c = scratch[node.c];
        Value v;
        const bool asFloat = types[i] == rule::ExprType::Float;
        switch (node.op) {
            case rule::ExprOp::Self:         v.i = own; break;
            case rule::ExprOp::Neighbour:    v.i = nbr[node.a]; break;
            case rule::ExprOp::Count:        v.i = static_cast<int32_t>(counts[node.a]); break;
            case rule::ExprOp::IntLiteral:   v.i = static_cast<int32_t>(node.ival); break;
            case rule::ExprOp::FloatLiteral: v.f = node.fval; break;
            case rule::ExprOp::Add: if (asFloat) v.f = a.f + b.f; else v.i = wrapAdd(a.i, b.i); break;
            case rule::ExprOp::Sub: if (asFloat) v.f = a.f - b.f; else v.i = wrapSub(a.i, b.i); break;
            case rule::ExprOp::Mul: if (asFloat) v.f = a.f * b.f; else v.i = wrapMul(a.i, b.i); break;
            case rule::ExprOp::Div:
                // Zero divisor is undefined in GLSL and a trap here, so both
                // paths call it zero.
                if (asFloat) v.f = b.f == 0.0f ? 0.0f : a.f / b.f;
                else         v.i = b.i == 0 ? 0 : (a.i == INT32_MIN && b.i == -1 ? INT32_MIN : a.i / b.i);
                break;
            case rule::ExprOp::Mod:
                if (asFloat) v.f = b.f == 0.0f ? 0.0f : std::fmod(a.f, b.f);
                else         v.i = b.i == 0 ? 0 : (a.i == INT32_MIN && b.i == -1 ? 0 : a.i % b.i);
                break;
            case rule::ExprOp::Eq: v.b = types[node.a] == rule::ExprType::Float ? a.f == b.f : a.i == b.i; break;
            case rule::ExprOp::Ne: v.b = types[node.a] == rule::ExprType::Float ? a.f != b.f : a.i != b.i; break;
            case rule::ExprOp::Lt: v.b = types[node.a] == rule::ExprType::Float ? a.f <  b.f : a.i <  b.i; break;
            case rule::ExprOp::Le: v.b = types[node.a] == rule::ExprType::Float ? a.f <= b.f : a.i <= b.i; break;
            case rule::ExprOp::Gt: v.b = types[node.a] == rule::ExprType::Float ? a.f >  b.f : a.i >  b.i; break;
            case rule::ExprOp::Ge: v.b = types[node.a] == rule::ExprType::Float ? a.f >= b.f : a.i >= b.i; break;
            case rule::ExprOp::And: v.b = a.b && b.b; break;
            case rule::ExprOp::Or:  v.b = a.b || b.b; break;
            case rule::ExprOp::Not: v.b = !a.b; break;
            case rule::ExprOp::Select: v = a.b ? b : c; break;
        }
        scratch[i] = v;
    }
    const int32_t result = scratch.back().i;
    const int32_t top = static_cast<int32_t>(rule.states) - 1;
    return static_cast<uint8_t>(result < 0 ? 0 : (result > top ? top : result));
}

}  // namespace

void cpuStep(const rule::CompiledRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next,
             uint64_t generation, CellMutation mutation) {
    assert(current.data() != next.data() && "step must not read the buffer it writes (AV-004)");
    assert(current.size() == spec.bytesPerBuffer() && next.size() == spec.bytesPerBuffer());
    assert(rule.dimensions == spec.dimensions);

    const uint32_t W = spec.width, H = spec.height, D = spec.depth;
    const uint32_t N = rule.neighbourCount();
    const uint16_t S = rule.states;

    // Scratch, allocated once per step rather than per cell. The step loop
    // itself allocates nothing.
    std::vector<uint8_t>  nbr(N);
    std::vector<uint32_t> counts(S > 1 ? S - 1u : 0u);
    const bool expression = rule.backend == rule::Backend::Codegen;
    std::vector<uint32_t> stateCounts(expression ? S : 0u);   // indexed by state, 0 included
    std::vector<Value>    scratch;

    auto cellAt = [&](uint32_t x, uint32_t y, uint32_t z) -> uint8_t {
        return current[(size_t{z} * H + y) * W + x];
    };

    for (uint32_t z = 0; z < D; ++z) {
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                // Gather in canonical order.
                for (uint32_t i = 0; i < N; ++i) {
                    const rule::Offset& o = rule.offsets[i];
                    const auto nx = resolve(int64_t{x} + o.dx, W, rule.boundary);
                    const auto ny = resolve(int64_t{y} + o.dy, H, rule.boundary);
                    const auto nz = resolve(int64_t{z} + o.dz, D, rule.boundary);
                    nbr[i] = (nx && ny && nz) ? cellAt(*nx, *ny, *nz) : uint8_t{0};
                }

                const uint8_t own = cellAt(x, y, z);
                if (expression) {
                    for (uint32_t& c : stateCounts) c = 0;
                    for (uint32_t i = 0; i < N; ++i) ++stateCounts[nbr[i]];
                    uint8_t out = evalExpression(rule, own, nbr, stateCounts, scratch);
                    if (mutation.threshold != 0) {
                        if (mutates(blockHash(x, y, z, generation, mutation), mutation)) {
                            out = static_cast<uint8_t>(mutatedState(hash32(x, y, z, generation, mutation.seedB), S));
                        }
                    }
                    next[(size_t{z} * H + y) * W + x] = out;
                    continue;
                }
                uint64_t index = 0;
                switch (rule.kind) {
                    case Kind::OuterTotalistic: {
                        for (uint32_t& c : counts) c = 0;
                        for (uint32_t i = 0; i < N; ++i) {
                            if (nbr[i] != 0) ++counts[nbr[i] - 1u];
                        }
                        index = rule.layout.indexOuterTotalistic(own, counts);
                        break;
                    }
                    case Kind::CountedTotalistic: {
                        uint32_t k = 0;
                        for (uint32_t i = 0; i < N; ++i) {
                            if (rule.counted[own].test(nbr[i])) ++k;
                        }
                        index = rule.layout.indexCounted(own, k);
                        break;
                    }
                    case Kind::Totalistic: {
                        uint32_t sum = own;
                        for (uint32_t i = 0; i < N; ++i) sum += nbr[i];
                        index = rule.layout.indexTotalistic(sum);
                        break;
                    }
                    case Kind::NonTotalistic:
                        index = rule.layout.indexNonTotalistic(own, nbr);
                        break;
                    case Kind::Expression:
                    case Kind::Continuous:
                        assert(false && "handled above, or refused at compile time");
                        break;
                }
                uint8_t out = rule.table[index];
                if (mutation.threshold != 0) {
                    if (mutates(blockHash(x, y, z, generation, mutation), mutation)) {
                        out = static_cast<uint8_t>(mutatedState(hash32(x, y, z, generation, mutation.seedB), S));
                    }
                }
                next[(size_t{z} * H + y) * W + x] = out;
            }
        }
    }
}

void cpuStep(const rule::CompiledRule& rule, core::HostGrid& grid, uint64_t generation, CellMutation mutation) {
    cpuStep(rule, grid.spec(), grid.current(), grid.next(), generation, mutation);
    grid.swap();
}

}  // namespace aether::sim
