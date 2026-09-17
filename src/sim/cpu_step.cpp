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

// Walks the arena in order, which is valid because every child precedes its
// parent. The twin of rule/glsl.cpp: the two must agree on every rule, and
// the backend equivalence test is what says they do (AV-007).
void evalArena(const rule::Expression& e, std::span<const rule::ExprType> types,
               ExprValue self, std::span<const uint8_t> nbr,
               std::span<const uint32_t> counts, std::vector<ExprValue>& scratch) {
    const auto& nodes = e.nodes;
    scratch.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const rule::ExprNode& node = nodes[i];
        const ExprValue& a = scratch[node.a];
        const ExprValue& b = scratch[node.b];
        const ExprValue& c = scratch[node.c];
        ExprValue v;
        const bool asFloat = types[i] == rule::ExprType::Float;
        switch (node.op) {
            case rule::ExprOp::Self:         v = self; break;
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
}

uint8_t evalExpression(const rule::CompiledRule& rule, uint8_t own, std::span<const uint8_t> nbr,
                       std::span<const uint32_t> counts, std::vector<ExprValue>& scratch) {
    ExprValue self;
    self.i = own;
    evalArena(rule.expression, rule.expressionTypes, self, nbr, counts, scratch);
    const int32_t result = scratch.back().i;
    const int32_t top = static_cast<int32_t>(rule.states) - 1;
    return static_cast<uint8_t>(result < 0 ? 0 : (result > top ? top : result));
}

}  // namespace

float evalGrowth(const rule::Expression& growth, std::span<const rule::ExprType> types,
                 float convolution, std::vector<ExprValue>& scratch) {
    ExprValue self;
    self.f = convolution;
    evalArena(growth, types, self, {}, {}, scratch);
    return scratch.back().f;
}


StepScratch::StepScratch(const rule::CompiledRule& rule)
    : neighbours(rule.neighbourCount()),
      counts(rule.states > 1 ? rule.states - 1u : 0u),
      stateCounts(rule.backend == rule::Backend::Codegen ? rule.states : 0u) {}

CellTransition stepCell(const rule::CompiledRule& rule, const core::GridSpec& spec,
                        std::span<const uint8_t> current,
                        uint32_t x, uint32_t y, uint32_t z,
                        uint64_t generation, CellMutation mutation,
                        StepScratch& scratch) {
    const uint32_t W = spec.width, H = spec.height, D = spec.depth;
    const uint32_t N = rule.neighbourCount();
    const uint16_t S = rule.states;

    auto cellAt = [&](uint32_t cx, uint32_t cy, uint32_t cz) -> uint8_t {
        return current[(size_t{cz} * H + cy) * W + cx];
    };

    // A continuous rule reads floats out of the same bytes, convolves rather
    // than indexes, and produces an increment rather than a state.
    if (rule.kind == rule::Kind::Continuous) {
        const std::span<const float> cells{reinterpret_cast<const float*>(current.data()),
                                           current.size() / sizeof(float)};
        auto valueAt = [&](uint32_t cx, uint32_t cy, uint32_t cz) -> float {
            return cells[(size_t{cz} * H + cy) * W + cx];
        };

        CellTransition t;
        t.ownValue = valueAt(x, y, z);
        double conv = double{rule.selfWeight} * t.ownValue;
        for (uint32_t i = 0; i < N; ++i) {
            const rule::Offset& o = rule.offsets[i];
            const auto nx = resolve(int64_t{x} + o.dx, W, rule.boundary);
            const auto ny = resolve(int64_t{y} + o.dy, H, rule.boundary);
            const auto nz = resolve(int64_t{z} + o.dz, D, rule.boundary);
            // Outside a zero boundary the cell is empty, which contributes
            // nothing, exactly as state 0 does on the discrete path.
            const float v = (nx && ny && nz) ? valueAt(*nx, *ny, *nz) : 0.0f;
            conv += double{rule.weights[i]} * v;
        }
        t.convolution = static_cast<float>(conv);
        t.increment = evalGrowth(rule.expression, rule.expressionTypes, t.convolution, scratch.expr);
        const float raw = t.ownValue + t.increment;
        t.nextValue = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);   // SPEC §1
        if (mutation.threshold != 0 && mutates(blockHash(x, y, z, generation, mutation), mutation)) {
            t.nextValue = mutatedValue(hash32(x, y, z, generation, mutation.seedB));
            t.mutated = true;
        }
        return t;
    }

    // Gather in canonical order.
    std::vector<uint8_t>& nbr = scratch.neighbours;
    for (uint32_t i = 0; i < N; ++i) {
        const rule::Offset& o = rule.offsets[i];
        const auto nx = resolve(int64_t{x} + o.dx, W, rule.boundary);
        const auto ny = resolve(int64_t{y} + o.dy, H, rule.boundary);
        const auto nz = resolve(int64_t{z} + o.dz, D, rule.boundary);
        nbr[i] = (nx && ny && nz) ? cellAt(*nx, *ny, *nz) : uint8_t{0};
    }

    CellTransition t;
    t.own = cellAt(x, y, z);

    if (rule.backend == rule::Backend::Codegen) {
        for (uint32_t& c : scratch.stateCounts) c = 0;
        for (uint32_t i = 0; i < N; ++i) ++scratch.stateCounts[nbr[i]];
        t.fromRule = evalExpression(rule, t.own, nbr, scratch.stateCounts, scratch.expr);
    } else {
        switch (rule.kind) {
            case Kind::OuterTotalistic: {
                for (uint32_t& c : scratch.counts) c = 0;
                for (uint32_t i = 0; i < N; ++i) {
                    if (nbr[i] != 0) ++scratch.counts[nbr[i] - 1u];
                }
                t.tableIndex = rule.layout.indexOuterTotalistic(t.own, scratch.counts);
                break;
            }
            case Kind::CountedTotalistic: {
                uint32_t k = 0;
                for (uint32_t i = 0; i < N; ++i) {
                    if (rule.counted[t.own].test(nbr[i])) ++k;
                }
                t.scalar = k;
                t.tableIndex = rule.layout.indexCounted(t.own, k);
                break;
            }
            case Kind::Totalistic: {
                uint32_t sum = t.own;
                for (uint32_t i = 0; i < N; ++i) sum += nbr[i];
                t.scalar = sum;
                t.tableIndex = rule.layout.indexTotalistic(sum);
                break;
            }
            case Kind::NonTotalistic:
                t.tableIndex = rule.layout.indexNonTotalistic(t.own, nbr);
                break;
            case Kind::Expression:
            case Kind::Continuous:
                assert(false && "handled above, or refused at compile time");
                break;
        }
        t.hasIndex = true;
        t.fromRule = rule.table[t.tableIndex];
    }

    t.next = t.fromRule;
    if (mutation.threshold != 0 && mutates(blockHash(x, y, z, generation, mutation), mutation)) {
        t.next = static_cast<uint8_t>(mutatedState(hash32(x, y, z, generation, mutation.seedB), S));
        t.mutated = true;
    }
    return t;
}

void cpuStep(const rule::CompiledRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next,
             uint64_t generation, CellMutation mutation) {
    assert(current.data() != next.data() && "step must not read the buffer it writes (AV-004)");
    assert(current.size() == spec.bytesPerBuffer() && next.size() == spec.bytesPerBuffer());
    assert(rule.dimensions == spec.dimensions);

    const uint32_t W = spec.width, H = spec.height, D = spec.depth;

    // Scratch, allocated once per step rather than per cell. The step loop
    // itself allocates nothing.
    StepScratch scratch(rule);

    const bool continuous = rule.kind == rule::Kind::Continuous;
    const std::span<float> out{reinterpret_cast<float*>(next.data()),
                               continuous ? next.size() / sizeof(float) : 0};

    for (uint32_t z = 0; z < D; ++z) {
        for (uint32_t y = 0; y < H; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                const CellTransition t =
                    stepCell(rule, spec, current, x, y, z, generation, mutation, scratch);
                const size_t i = (size_t{z} * H + y) * W + x;
                if (continuous) out[i] = t.nextValue;
                else            next[i] = t.next;
            }
        }
    }
}

void cpuStep(const rule::CompiledRule& rule, core::HostGrid& grid, uint64_t generation, CellMutation mutation) {
    cpuStep(rule, grid.spec(), grid.current(), grid.next(), generation, mutation);
    grid.swap();
}

}  // namespace aether::sim
