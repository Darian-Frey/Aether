#include "sim/cpu_step.hpp"

#include "sim/boundary.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace aether::sim {

using rule::Kind;

namespace {

// Wrapping 32-bit arithmetic, so the oracle overflows exactly where GLSL
// does rather than wandering into undefined behaviour (SPEC §6).
int32_t wrapAdd(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t wrapSub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t wrapMul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

// Flush-to-zero on subnormals, the twin of the same statement in
// rule/glsl.cpp. GLSL does not require support for values below FLT_MIN and
// both GPUs here flush them, so the oracle must too or a decaying float
// expression parts company with the shader (BUG-021, SPEC §6). The comparison
// is on the magnitude, so a negative subnormal goes to zero as well.
float flushSubnormal(float v) {
    return std::fabs(v) < std::numeric_limits<float>::min() ? 0.0f : v;
}

// Everything an expression can read, gathered before the walk so that
// evalArena knows nothing about grids, boundaries or field storage. It became a
// struct when fields arrived (F-031): five spans passed positionally are five
// chances to swap two of them at a call site and have it still compile.
struct ExprInputs {
    ExprValue                  self;
    std::span<const uint8_t>   nbr;
    std::span<const uint32_t>  counts;
    std::span<const ExprValue> fieldSelf;   // one per declared field
    std::span<const ExprValue> fieldNbr;    // field f at neighbour i: f*N + i
    uint32_t                   neighbours = 0;   // N, the stride of fieldNbr
};

// Walks the arena in order, which is valid because every child precedes its
// parent. The twin of rule/glsl.cpp: the two must agree on every rule, and
// the backend equivalence test is what says they do (AV-007).
void evalArena(const rule::Expression& e, std::span<const rule::ExprType> types,
               const ExprInputs& in, std::vector<ExprValue>& scratch) {
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
            case rule::ExprOp::Self:         v = in.self; break;
            case rule::ExprOp::Neighbour:    v.i = in.nbr[node.a]; break;
            case rule::ExprOp::Count:        v.i = static_cast<int32_t>(in.counts[node.a]); break;
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
            // The gathered value already has the member the field's declared
            // cell type calls for, which is the same type the validator gave
            // the node, so there is nothing to convert here (F-031).
            case rule::ExprOp::FieldSelf:      v = in.fieldSelf[node.a]; break;
            case rule::ExprOp::FieldNeighbour:
                v = in.fieldNbr[size_t{node.a} * in.neighbours + node.b];
                break;
        }
        if (types[i] == rule::ExprType::Float) v.f = flushSubnormal(v.f);
        scratch[i] = v;
    }
}

uint8_t evalExpression(const rule::CompiledRule& rule, const ExprInputs& in,
                       std::vector<ExprValue>& scratch) {
    evalArena(rule.expression, rule.expressionTypes, in, scratch);
    const int32_t result = scratch.back().i;
    const int32_t top = static_cast<int32_t>(rule.states) - 1;
    return static_cast<uint8_t>(result < 0 ? 0 : (result > top ? top : result));
}

// One field's write expression. The state's clamp is to the state range; a
// field has no such range, so a u8 field clamps to the width of its storage
// and an f32 field is written as computed — a resource has no natural ceiling
// and neither has an R32F texture, so clamping one to [0,1] as SPEC §1 clamps
// a continuous *state* would make the field useless (F-031).
ExprValue evalFieldWrite(const rule::CompiledField& field, const ExprInputs& in,
                         std::vector<ExprValue>& scratch) {
    evalArena(*field.write, field.writeTypes, in, scratch);
    ExprValue v = scratch.back();
    if (field.cell_type != core::CellType::F32) {
        v.i = v.i < 0 ? 0 : (v.i > 255 ? 255 : v.i);
    }
    return v;
}

}  // namespace

float evalGrowth(const rule::Expression& growth, std::span<const rule::ExprType> types,
                 float convolution, std::vector<ExprValue>& scratch) {
    ExprInputs in;
    in.self.f = convolution;
    evalArena(growth, types, in, scratch);
    return scratch.back().f;
}


StepScratch::StepScratch(const rule::CompiledRule& rule)
    : neighbours(rule.neighbourCount()),
      counts(rule.states > 1 ? rule.states - 1u : 0u),
      stateCounts(rule.backend == rule::Backend::Codegen ? rule.states : 0u),
      fieldSelf(rule.fields.size()),
      fieldNbr(rule.fields.size() * rule.neighbourCount()),
      fieldNext(rule.fields.size()) {}

CellTransition stepCell(const rule::CompiledRule& rule, const core::GridSpec& spec,
                        std::span<const uint8_t> current,
                        uint32_t x, uint32_t y, uint32_t z,
                        uint64_t generation, CellMutation mutation,
                        StepScratch& scratch, FieldReads fields) {
    const uint32_t W = spec.width, H = spec.height, D = spec.depth;
    const uint32_t N = rule.neighbourCount();
    const uint16_t S = rule.states;

    auto cellAt = [&](uint32_t cx, uint32_t cy, uint32_t cz) -> uint8_t {
        return current[(size_t{cz} * H + cy) * W + cx];
    };

    // A continuous rule reads floats out of the same bytes, convolves rather
    // than indexes, and produces an increment rather than a state.
    if (rule.kind == rule::Kind::Continuous) {
        assert(rule.fields.empty() && "refused at compile time (F-031)");
        const std::span<const float> cells{reinterpret_cast<const float*>(current.data()),
                                           current.size() / sizeof(float)};
        auto valueAt = [&](uint32_t cx, uint32_t cy, uint32_t cz) -> float {
            return cells[(size_t{cz} * H + cy) * W + cx];
        };

        CellTransition t;
        t.ownValue = valueAt(x, y, z);
        // Accumulated in float, in offset order, because that is all the GPU
        // can do and the two paths must agree bit for bit (AV-007). A double
        // here would be more accurate and would disagree with the shader,
        // which is the worse of the two.
        float conv = flushSubnormal(rule.selfWeight * t.ownValue);
        for (uint32_t i = 0; i < N; ++i) {
            const rule::Offset& o = rule.offsets[i];
            const auto nx = resolve(int64_t{x} + o.dx, W, rule.boundary);
            const auto ny = resolve(int64_t{y} + o.dy, H, rule.boundary);
            const auto nz = resolve(int64_t{z} + o.dz, D, rule.boundary);
            // Outside a zero boundary the cell is empty, which contributes
            // nothing, exactly as state 0 does on the discrete path.
            const float v = (nx && ny && nz) ? valueAt(*nx, *ny, *nz) : 0.0f;
            // Each partial sum flushed, exactly as continuous_step.comp does
            // it: a weight times a near-zero value is where this reaches the
            // subnormal range (BUG-021).
            const float term = flushSubnormal(rule.weights[i] * v);
            conv = flushSubnormal(conv + term);
        }
        t.convolution = conv;
        t.increment = evalGrowth(rule.expression, rule.expressionTypes, t.convolution, scratch.expr);
        const float raw = flushSubnormal(t.ownValue + t.increment);
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

    // Auxiliary fields, read with the same boundary resolution as the state and
    // from the same generation, so that everything a cell decides it decides
    // against one reading of the world (F-031, D-022). Outside a zero boundary
    // a field reads zero, exactly as state 0 does.
    const size_t F = rule.fields.size();
    if (F != 0) {
        assert(fields.size() == F && "a multi-field rule needs its field buffers");
        auto fieldAt = [&](size_t f, size_t idx) -> ExprValue {
            ExprValue v;
            if (rule.fields[f].cell_type == core::CellType::F32) {
                float value = 0.0f;
                std::memcpy(&value, fields[f].data() + idx * sizeof(float), sizeof(float));
                v.f = value;
            } else {
                v.i = fields[f][idx];
            }
            return v;
        };
        const size_t here = (size_t{z} * H + y) * W + x;
        for (size_t f = 0; f < F; ++f) scratch.fieldSelf[f] = fieldAt(f, here);
        for (uint32_t i = 0; i < N; ++i) {
            const rule::Offset& o = rule.offsets[i];
            const auto nx = resolve(int64_t{x} + o.dx, W, rule.boundary);
            const auto ny = resolve(int64_t{y} + o.dy, H, rule.boundary);
            const auto nz = resolve(int64_t{z} + o.dz, D, rule.boundary);
            const bool inside = nx && ny && nz;
            const size_t idx = inside ? (size_t{*nz} * H + *ny) * W + *nx : 0;
            for (size_t f = 0; f < F; ++f) {
                scratch.fieldNbr[f * N + i] = inside ? fieldAt(f, idx) : ExprValue{};
            }
        }
    }

    ExprInputs in;
    in.self.i = t.own;
    in.nbr = nbr;
    in.counts = scratch.stateCounts;
    in.fieldSelf = scratch.fieldSelf;
    in.fieldNbr = scratch.fieldNbr;
    in.neighbours = N;

    if (rule.backend == rule::Backend::Codegen) {
        for (uint32_t& c : scratch.stateCounts) c = 0;
        for (uint32_t i = 0; i < N; ++i) ++scratch.stateCounts[nbr[i]];
        t.fromRule = evalExpression(rule, in, scratch.expr);
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

    // Each field's own expression, over the same gathered neighbourhood. A
    // field the rule leaves alone keeps its value; cell mutation is the
    // state's (SPEC §9.2) and does not touch a field, which is what keeps a
    // resource conserved under drift (AV-018).
    for (size_t f = 0; f < F; ++f) {
        scratch.fieldNext[f] = rule.fields[f].write
                                   ? evalFieldWrite(rule.fields[f], in, scratch.expr)
                                   : scratch.fieldSelf[f];
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
             uint64_t generation, CellMutation mutation,
             FieldReads fields, FieldWrites fieldsNext) {
    assert(current.data() != next.data() && "step must not read the buffer it writes (AV-004)");
    assert(current.size() == spec.bytesPerBuffer() && next.size() == spec.bytesPerBuffer());
    assert(rule.dimensions == spec.dimensions);

    const size_t F = rule.fields.size();
    assert(fields.size() == F && fieldsNext.size() == F && "one buffer pair per declared field");
    for (size_t f = 0; f < F; ++f) {
        // The same aliasing rule as the state's, for the same reason: a field
        // read after it has been written this generation is a different
        // automaton (AV-004).
        assert(fields[f].data() != fieldsNext[f].data());
        const size_t want = spec.cellCount() * core::cellBytes(rule.fields[f].cell_type);
        assert(fields[f].size() == want && fieldsNext[f].size() == want);
        (void)want;
    }

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
                    stepCell(rule, spec, current, x, y, z, generation, mutation, scratch, fields);
                const size_t i = (size_t{z} * H + y) * W + x;
                if (continuous) out[i] = t.nextValue;
                else            next[i] = t.next;
                // Every field is written every generation, including one the
                // rule does not write: on a ping-pong pair the next buffer is
                // last generation's, so keeping a value means copying it.
                for (size_t f = 0; f < F; ++f) {
                    const ExprValue& v = scratch.fieldNext[f];
                    if (rule.fields[f].cell_type == core::CellType::F32) {
                        std::memcpy(fieldsNext[f].data() + i * sizeof(float), &v.f, sizeof(float));
                    } else {
                        fieldsNext[f][i] = static_cast<uint8_t>(v.i);
                    }
                }
            }
        }
    }
}

void cpuStep(const rule::CompiledRule& rule, core::HostGrid& grid, uint64_t generation, CellMutation mutation) {
    // A HostGrid is the state field alone, so this overload is for rules that
    // declare none; field storage on the grid is F-031's step 4.
    assert(rule.fields.empty() && "a multi-field rule needs the field-buffer overload");
    cpuStep(rule, grid.spec(), grid.current(), grid.next(), generation, mutation);
    grid.swap();
}

}  // namespace aether::sim
