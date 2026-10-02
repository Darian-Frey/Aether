// Inheritance at birth (F-033 step 2).
//
// The engine derives a newly born cell's genome from its live neighbours and
// mutates it per bit. What this file pins: each of the three schemes does what
// it says, the draws come from stream B so a run reproduces without storing
// anything, grouping works as it does for cell mutation, only the live bits are
// touched, and — the case that matters most — a genome actually *sweeps* a grid
// under selection, which is what distinguishes this from the two mutation
// controls that came before it.

#include "rule/compile.hpp"
#include "rule/dsl.hpp"
#include "sim/cpu_step.hpp"
#include "sim/genome.hpp"
#include "sim/simulation.hpp"
#include "support/fields.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <set>
#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

namespace {

core::GridSpec spec2d(uint32_t w, uint32_t h) {
    core::GridSpec s;
    s.dimensions = 2;
    s.width = w;
    s.height = h;
    return s;
}

// A Life-like genome: nine birth bits then nine survival bits, so the rule is
// `(genome >> (alive ? count + 9 : count)) & 1`. Eighteen bits, which is the
// reference case F-033 names — and a convention of *this rule* rather than
// anything the engine knows (D-025).
constexpr uint32_t kBits = 18;

uint32_t maskFor(std::initializer_list<uint32_t> birth, std::initializer_list<uint32_t> survive) {
    uint32_t m = 0;
    for (uint32_t b : birth)   m |= 1u << b;
    for (uint32_t s : survive) m |= 1u << (9 + s);
    return m;
}

rule::RuleIR lifeLikeGenomeRule() {
    rule::RuleIR ir;
    ir.states = 2;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    rule::Field gene;
    gene.name = "genome";
    gene.cell_type = core::CellType::U32;
    ir.fields = {gene};
    ir.genome = rule::Genome{0, kBits};

    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::FieldSelf, 0},                  // 0  the mask
        {rule::ExprOp::Count, 1},                      // 1  live neighbours
        {rule::ExprOp::Self},                          // 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},        // 3
        {rule::ExprOp::Eq, 2, 3},                      // 4  alive
        {rule::ExprOp::IntLiteral, 0, 0, 0, 9},        // 5
        {rule::ExprOp::Add, 1, 5},                     // 6
        {rule::ExprOp::Select, 4, 6, 1},               // 7  which half
        {rule::ExprOp::Shr, 0, 7},                     // 8
        {rule::ExprOp::BitAnd, 8, 3},                  // 9
    };
    ir.transition = e;
    return ir;
}

rule::CompiledRule compiled(const rule::RuleIR& ir) {
    auto r = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&r)) FAIL(e->message);
    return std::get<rule::CompiledRule>(r);
}

}  // namespace

TEST_CASE("majority takes each bit from more than half the parents", "[genome]") {
    sim::GenomeParams p;
    p.scheme = sim::Inheritance::Majority;

    // Three parents. Bit 0 is set in all three, bit 1 in two, bit 2 in one.
    const std::vector<uint32_t> parents{0b011u, 0b011u, 0b101u};
    const uint32_t child = sim::inherit(0, parents, 4, 1, 1, 0, 7, p);
    CHECK((child & 1u) == 1u);          // 3 of 3
    CHECK(((child >> 1) & 1u) == 1u);   // 2 of 3
    CHECK(((child >> 2) & 1u) == 0u);   // 1 of 3

    // A tie leaves the bit clear. Arbitrary, but it has to be some fixed thing
    // or the two paths would be free to differ.
    const std::vector<uint32_t> two{0b1u, 0b0u};
    CHECK(sim::inherit(0, two, 4, 1, 1, 0, 7, p) == 0u);
}

TEST_CASE("a random parent is copied whole and a crossover is not", "[genome]") {
    sim::GenomeParams p;
    p.seedB = 0x1234u;

    // Two parents with no bits in common, so whatever comes out says where it
    // came from.
    const std::vector<uint32_t> parents{0x0000FFFFu, 0xFFFF0000u};

    p.scheme = sim::Inheritance::RandomParent;
    std::set<uint32_t> seen;
    for (uint32_t x = 0; x < 64; ++x) {
        const uint32_t child = sim::inherit(0, parents, 32, x, 0, 0, 1, p);
        // Whole, which for these two parents means exactly one of them.
        CHECK((child == parents[0] || child == parents[1]));
        seen.insert(child);
    }
    CHECK(seen.size() == 2);   // and the draw is actually drawing

    p.scheme = sim::Inheritance::Crossover;
    bool mixed = false;
    for (uint32_t x = 0; x < 64 && !mixed; ++x) {
        const uint32_t child = sim::inherit(0, parents, 32, x, 0, 0, 1, p);
        // Uniform crossover: bits from both, so neither parent entire.
        if (child != parents[0] && child != parents[1]) mixed = true;
    }
    CHECK(mixed);
}

TEST_CASE("mutation touches the live bits and only those", "[genome]") {
    sim::GenomeParams p;
    p.scheme = sim::Inheritance::Majority;
    p.seedB = 99;
    p.threshold = sim::mutationThreshold(1.0);   // every bit, every birth

    const std::vector<uint32_t> parents{0u};
    // Eight live bits of a 32-bit field: everything above them must stay clear,
    // or a genome hash would be a hash of noise and F-033's visible lineages
    // would be lineages that are not there.
    const uint32_t child = sim::inherit(0, parents, 8, 3, 4, 0, 11, p);
    CHECK(child == 0xFFu);

    // And with no mutation at all the parents decide entirely.
    p.threshold = 0;
    CHECK(sim::inherit(0, parents, 8, 3, 4, 0, 11, p) == 0u);
}

TEST_CASE("inheritance is reproducible and grouped when asked", "[genome]") {
    sim::GenomeParams p;
    p.scheme = sim::Inheritance::Crossover;
    p.seedB = 0xabcdu;
    p.threshold = sim::mutationThreshold(0.2);
    const std::vector<uint32_t> parents{0x0F0F0F0Fu, 0xF0F0F0F0u};

    // Stream B is stateless, so the same cell at the same generation gives the
    // same child however many times it is asked.
    const uint32_t a = sim::inherit(0, parents, 32, 5, 6, 0, 42, p);
    CHECK(sim::inherit(0, parents, 32, 5, 6, 0, 42, p) == a);
    CHECK(sim::inherit(0, parents, 32, 5, 7, 0, 42, p) != a);   // and varies by cell
    CHECK(sim::inherit(0, parents, 32, 5, 6, 0, 43, p) != a);   // and by generation

    // Grouped: the *mutation* is shared across a block, so two cells in one
    // block differ only by their parent draws. Same parents and the same
    // single parent scheme means the same child.
    p.scheme = sim::Inheritance::Majority;   // no parent draw at all
    p.blockShift = 2;                        // blocks of four per axis
    const uint32_t inBlock = sim::inherit(0, parents, 32, 4, 4, 0, 1, p);
    CHECK(sim::inherit(0, parents, 32, 5, 5, 0, 1, p) == inBlock);   // same block
    CHECK(sim::inherit(0, parents, 32, 9, 9, 0, 1, p) != inBlock);   // another block
}

TEST_CASE("no parents means nothing to inherit", "[genome]") {
    // A cell that mutated into existence away from anything living. Keeping what
    // the field holds is the only answer that invents nothing.
    sim::GenomeParams p;
    p.scheme = sim::Inheritance::Crossover;
    p.threshold = sim::mutationThreshold(1.0);
    CHECK(sim::inherit(0xDEADu, {}, 32, 1, 2, 0, 3, p) == 0xDEADu);
}

TEST_CASE("a birth takes its genome from its live neighbours", "[genome]") {
    // Through the stepper, which is where the parent list is actually built.
    const rule::RuleIR ir = lifeLikeGenomeRule();
    const rule::CompiledRule rule = compiled(ir);
    REQUIRE(rule.genome.has_value());
    CHECK(rule.genome->bits == kBits);

    const core::GridSpec spec = spec2d(8, 8);
    core::HostGrid host(spec);
    aether::test::HostFields fields(rule, spec.cellCount());

    // Three live cells in an L, which under B3 births the fourth corner. All
    // three carry Conway; the empty cell carries nothing.
    const uint32_t life = maskFor({3}, {2, 3});
    auto at = [&](uint32_t x, uint32_t y) { return size_t{y} * spec.width + x; };
    for (const auto [x, y] : {std::pair<uint32_t, uint32_t>{1, 1}, {2, 1}, {1, 2}}) {
        host.current()[at(x, y)] = 1;
        fields.setU32(0, at(x, y), life);
    }
    REQUIRE(fields.u32(0, at(2, 2)) == 0u);

    sim::GenomeParams gp;
    gp.scheme = sim::Inheritance::Majority;   // all three agree, so no draw matters
    cpuStep(rule, spec, host.current(), host.next(), 0, {}, fields.reads(), fields.writes(),
            {}, nullptr, gp);
    host.swap();
    fields.swap();

    CHECK(host.current()[at(2, 2)] == 1);          // born
    CHECK(fields.u32(0, at(2, 2)) == life);        // and carrying its parents' rule
    CHECK(fields.u32(0, at(1, 1)) == life);        // the parents kept theirs
}

TEST_CASE("a genome sweeps a grid under selection", "[genome][gpu]") {
    GlContext gl;
    requireGl(gl);

    // The case that distinguishes this feature from the two mutation controls
    // before it. Two rules share a grid: Conway, and a rule that cannot sustain
    // itself. Whichever reproduces takes the grid, and nothing applies the
    // outcome — it is selected.
    const core::GridSpec spec = spec2d(64, 64);
    auto made = sim::Simulation::create(spec, lifeLikeGenomeRule(), sim::Path::Cpu, 5u, 11u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);

    sim::GenomeParams gp;
    gp.scheme = sim::Inheritance::Majority;
    gp.threshold = 0;    // no mutation: the sweep is selection and nothing else
    s.setGenome(gp);

    const uint32_t life   = maskFor({3}, {2, 3});
    const uint32_t barren = maskFor({}, {8});   // never born, survives only when boxed in
    s.fillRandom(std::vector<double>{0.35});

    // Half the grid each, so neither starts with an advantage of numbers.
    auto cells = s.host().current();
    for (uint32_t y = 0; y < spec.height; ++y) {
        for (uint32_t x = 0; x < spec.width; ++x) {
            const size_t i = size_t{y} * spec.width + x;
            const uint32_t which = x < spec.width / 2 ? life : barren;
            std::memcpy(s.fieldHost(0).current().data() + i * 4, &which, 4);
            (void)cells;
        }
    }
    s.commitHost();

    auto share = [&] {
        size_t alive = 0, carryingLife = 0;
        for (size_t i = 0; i < spec.cellCount(); ++i) {
            if (s.host().current()[i] == 0) continue;
            ++alive;
            uint32_t g = 0;
            std::memcpy(&g, s.fieldHost(0).current().data() + i * 4, 4);
            if (g == life) ++carryingLife;
        }
        return std::pair{alive, carryingLife};
    };

    const auto [aliveBefore, lifeBefore] = share();
    REQUIRE(aliveBefore > 0);
    // Roughly half, since the grid was seeded uniformly and split down the middle.
    CHECK(lifeBefore * 4 < aliveBefore * 3);

    for (int g = 0; g < 400; ++g) s.step();

    const auto [aliveAfter, lifeAfter] = share();
    REQUIRE(aliveAfter > 0);                     // the grid did not die out

    // The *share*, not the count. Life thins a random soup to a few percent
    // whatever genome it carries, so the absolute number of Conway carriers falls
    // even as Conway wins — the first version of this test compared counts and
    // read a sweep as a collapse. What selection changes is the proportion.
    const double before = static_cast<double>(lifeBefore) / static_cast<double>(aliveBefore);
    const double after  = static_cast<double>(lifeAfter)  / static_cast<double>(aliveAfter);
    CHECK(before < 0.75);
    CHECK(after > 0.9);
    CHECK(after > before);
}
