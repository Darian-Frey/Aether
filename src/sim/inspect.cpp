#include "sim/inspect.hpp"

#include "sim/boundary.hpp"

namespace aether::sim {

namespace {

// Which conditional answered. The arena holds every node's value, because
// `evalArena` walks the whole tree bottom-up, so this is a read of what the
// oracle already decided rather than a second evaluation of the conditions.
// Descending from the root: a Select whose test held supplied the answer and
// is the clause; one whose test failed hands the question to its alternative.
void findClause(const rule::Expression& e, const std::vector<ExprValue>& arena, Inspection& out) {
    if (e.nodes.empty() || arena.size() != e.nodes.size()) return;
    size_t at = e.nodes.size() - 1;
    for (;;) {
        const rule::ExprNode& n = e.nodes[at];
        if (n.op != rule::ExprOp::Select) {
            // The chain ran out. If we got here by failing every test, the
            // last alternative is the answer and there is no firing clause.
            out.clauseIsDefault = at != e.nodes.size() - 1;
            return;
        }
        if (arena[n.a].b) {
            out.clause = at;
            return;
        }
        at = n.c;
    }
}

}  // namespace

Inspection inspect(const rule::CompiledRule& rule, const core::GridSpec& spec,
                   std::span<const uint8_t> cells,
                   uint32_t x, uint32_t y, uint32_t z,
                   uint64_t generation, CellMutation mutation,
                   StepScratch& scratch) {
    Inspection out;
    out.x = x; out.y = y; out.z = z;
    out.generation = generation;

    // The whole transition, from the one implementation of it. Everything
    // below reads what this left behind; nothing below recomputes it.
    out.transition = stepCell(rule, spec, cells, x, y, z, generation, mutation, scratch);

    const uint32_t N = rule.neighbourCount();
    const bool continuous = rule.kind == rule::Kind::Continuous;
    const std::span<const float> floats{reinterpret_cast<const float*>(cells.data()),
                                        cells.size() / sizeof(float)};

    out.neighbours.resize(N);
    for (uint32_t i = 0; i < N; ++i) {
        const rule::Offset& o = rule.offsets[i];
        NeighbourCell& nc = out.neighbours[i];
        nc.offset = o;

        // The same resolution the step used, from the same function, so a
        // cell on an edge explains the way it actually behaves (AV-017).
        const auto nx = resolve(int64_t{x} + o.dx, spec.width, rule.boundary);
        const auto ny = resolve(int64_t{y} + o.dy, spec.height, rule.boundary);
        const auto nz = resolve(int64_t{z} + o.dz, spec.depth, rule.boundary);
        if (!nx || !ny || !nz) {
            nc.outside = true;
            continue;
        }
        nc.x = *nx; nc.y = *ny; nc.z = *nz;
        nc.wrapped = int64_t{*nx} != int64_t{x} + o.dx || int64_t{*ny} != int64_t{y} + o.dy ||
                     int64_t{*nz} != int64_t{z} + o.dz;
        const size_t at = (size_t{nc.z} * spec.height + nc.y) * spec.width + nc.x;
        if (continuous) nc.value = floats[at];
        else            nc.state = cells[at];
    }

    // What the kind counted. `stepCell` left its own working in the scratch,
    // so these are the numbers the table was indexed with, not a recount.
    Reduction& r = out.reduction;
    if (continuous) return out;
    if (rule.backend == rule::Backend::Codegen) {
        r.perState.assign(scratch.stateCounts.begin(), scratch.stateCounts.end());
        r.perStateFromZero = true;
        r.hasPerState = true;
        findClause(rule.expression, scratch.expr, out);
        return out;
    }
    switch (rule.kind) {
        case rule::Kind::OuterTotalistic:
            r.perState.assign(scratch.counts.begin(), scratch.counts.end());
            r.perStateFromZero = false;
            r.hasPerState = true;
            break;
        case rule::Kind::CountedTotalistic:
            r.hasScalar = true;
            r.scalar = out.transition.scalar;
            r.scalarMeans = "neighbours in the counted set for this state";
            break;
        case rule::Kind::Totalistic:
            r.hasScalar = true;
            r.scalar = out.transition.scalar;
            r.scalarMeans = "sum of this cell and its neighbours";
            break;
        case rule::Kind::NonTotalistic:
            // The signature is the neighbour states themselves, already above.
            break;
        case rule::Kind::Expression:
        case rule::Kind::Continuous:
            break;
    }
    return out;
}

}  // namespace aether::sim
