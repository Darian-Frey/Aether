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
    for (const auto& [x, y] : {std::pair<uint32_t, uint32_t>{1, 1}, {2, 1}, {1, 2}}) {
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
    if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
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

// --- Similarity-biased birth (F-035) ----------------------------------------

TEST_CASE("disagreement is zero for parents that agree and full for a clean split", "[genome]") {
    // The measure, on hand-built parent lists. Integer throughout, so these are
    // exact equalities rather than tolerances — which is the point of its being
    // integer: a float measure would put the two execution paths a rounding
    // error apart (AV-015).
    const uint32_t full = sim::kBirthBiasFull;

    // Unanimous, whatever they agree on.
    CHECK(sim::disagreement(std::vector<uint32_t>{0b1011, 0b1011, 0b1011}, 4) == 0);
    CHECK(sim::disagreement(std::vector<uint32_t>{0, 0, 0, 0}, 4) == 0);

    // Fewer than two parents cannot disagree.
    CHECK(sim::disagreement(std::vector<uint32_t>{0b0101}, 4) == 0);
    CHECK(sim::disagreement(std::vector<uint32_t>{}, 4) == 0);

    // An even split on every bit is the maximum: four bits at full strength.
    CHECK(sim::disagreement(std::vector<uint32_t>{0b1111, 0b0000}, 4) == 4 * full);

    // One bit of four split evenly, the rest unanimous.
    CHECK(sim::disagreement(std::vector<uint32_t>{0b0001, 0b0000}, 4) == full);

    // A lone dissenter among four counts a quarter, not a half: the minority is
    // one of four, doubled.
    CHECK(sim::disagreement(std::vector<uint32_t>{0b1, 0b0, 0b0, 0b0}, 1) == (1u * 2u * full) / 4u);

    // Bits the rule does not use are not measured, which is the same reason
    // mutation does not touch them.
    CHECK(sim::disagreement(std::vector<uint32_t>{0xFFFFFFFFu, 0x00000000u}, 2) == 2 * full);
}

TEST_CASE("a birth bias of zero makes no decision and draws nothing", "[genome]") {
    // Off by construction rather than by a small probability. This is what lets
    // every session written before F-035 replay unchanged: the decision is not
    // merely always true, it is never taken, so stream B is untouched (AV-006).
    sim::GenomeParams p;
    p.seedB = 7;
    p.birthBias = 0;
    const std::vector<uint32_t> split{0b1111, 0b0000, 0b1010};
    for (uint32_t x = 0; x < 64; ++x) {
        CHECK(sim::birthAllowed(split, 4, x, 0, 0, 3, p));
    }

    // And unanimous parents are never refused, at any strength: there is
    // nothing for the bias to object to.
    p.birthBias = sim::kBirthBiasFull;
    const std::vector<uint32_t> agreed{0b1011, 0b1011, 0b1011};
    for (uint32_t x = 0; x < 64; ++x) {
        CHECK(sim::birthAllowed(agreed, 4, x, 0, 0, 3, p));
    }
}

TEST_CASE("the birth bias refuses in proportion to how split the parents are", "[genome]") {
    // The behavioural claim, measured rather than asserted about one site: over
    // many sites, a more divided neighbourhood is refused more often, and the
    // rate tracks the strength.
    auto refusalRate = [](const std::vector<uint32_t>& parents, uint16_t bias, uint32_t bits) {
        sim::GenomeParams p;
        p.seedB = 99;
        p.birthBias = bias;
        int refused = 0;
        const int trials = 4000;
        for (int i = 0; i < trials; ++i) {
            const auto x = static_cast<uint32_t>(i % 64);
            const auto y = static_cast<uint32_t>(i / 64);
            if (!sim::birthAllowed(parents, bits, x, y, 0, 11, p)) ++refused;
        }
        return static_cast<double>(refused) / trials;
    };

    const std::vector<uint32_t> even{0b1111, 0b0000};          // every bit split
    const std::vector<uint32_t> oneBit{0b0001, 0b0000};        // one bit of four
    const uint16_t full = sim::kBirthBiasFull;

    // At full strength a wholly split pair is always refused, and a pair
    // differing in one bit of four about a quarter of the time.
    CHECK(refusalRate(even, full, 4) > 0.99);
    CHECK(refusalRate(oneBit, full, 4) > 0.20);
    CHECK(refusalRate(oneBit, full, 4) < 0.30);

    // Half the strength, half the refusals.
    CHECK(refusalRate(even, static_cast<uint16_t>(full / 2), 4) > 0.45);
    CHECK(refusalRate(even, static_cast<uint16_t>(full / 2), 4) < 0.55);

    // More disagreement is always refused at least as often as less.
    CHECK(refusalRate(even, full, 4) >= refusalRate(oneBit, full, 4));
}

TEST_CASE("the birth bias puts births where the parents agree", "[genome]") {
    // The case that keeps this file honest (IMP-011): everything above would
    // pass if `birthAllowed` were never reached from the step, so this drives
    // the oracle and compares two runs differing only in the bias.
    //
    // It measures the mechanism rather than a hoped-for consequence. The first
    // version of this test asserted that neighbouring live cells more often
    // share a genome with the bias on, and passed by comparing 0.000 with
    // 0.000 — a Life-like rule separates its lineages within a few dozen
    // generations whatever the bias does, so there was nothing left to
    // consolidate. What the feature actually promises is narrower and
    // checkable: of the births that happen, fewer have parents that disagree.
    const rule::RuleIR ir = lifeLikeGenomeRule();
    const rule::CompiledRule rule = compiled(ir);
    const core::GridSpec spec = spec2d(64, 64);

    // Two genomes that differ in seven of their eighteen bits. Conway and
    // HighLife would not do: they differ in *one* bit, so by this measure they
    // are 94% alike and the bias barely objects to mixing them — which is the
    // measure being right rather than weak.
    const uint32_t a = maskFor({3}, {2, 3});
    const uint32_t b = maskFor({2, 4, 6}, {1, 4, 5, 7});
    auto at = [&](uint32_t x, uint32_t y) { return size_t{y} * spec.width + x; };

    struct Result { long births = 0; long split = 0; double meanDisagreement = 0; };
    auto run = [&](uint16_t bias) {
        core::HostGrid host(spec);
        aether::test::HostFields fields(rule, spec.cellCount());
        sim::Pcg32 rng(5);
        for (uint32_t y = 0; y < spec.height; ++y) {
            for (uint32_t x = 0; x < spec.width; ++x) {
                host.current()[at(x, y)] = rng.unit() < 0.35 ? 1 : 0;
                fields.setU32(0, at(x, y), rng.unit() < 0.5 ? a : b);
            }
        }
        sim::GenomeParams gp;
        gp.scheme = sim::Inheritance::Majority;
        gp.seedB = 4;
        gp.birthBias = bias;

        Result out;
        double total = 0;
        for (uint64_t g = 0; g < 200; ++g) {
            const std::vector<uint8_t> before(host.current().begin(), host.current().end());
            std::vector<uint32_t> genomesBefore(spec.cellCount());
            for (size_t i = 0; i < spec.cellCount(); ++i) genomesBefore[i] = fields.u32(0, i);

            cpuStep(rule, spec, host.current(), host.next(), g, {},
                    fields.reads(), fields.writes(), {}, nullptr, gp);
            host.swap();
            fields.swap();

            // Every cell that went from dead to alive, and how split the
            // neighbours it inherited from were. Away from the edges, so the
            // boundary rule plays no part in the count.
            for (uint32_t y = 1; y + 1 < spec.height; ++y) {
                for (uint32_t x = 1; x + 1 < spec.width; ++x) {
                    if (before[at(x, y)] != 0 || host.current()[at(x, y)] == 0) continue;
                    std::vector<uint32_t> parents;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            const size_t k = at(static_cast<uint32_t>(static_cast<int>(x) + dx),
                                                static_cast<uint32_t>(static_cast<int>(y) + dy));
                            if (before[k]) parents.push_back(genomesBefore[k]);
                        }
                    }
                    const uint32_t d = sim::disagreement(parents, kBits);
                    ++out.births;
                    if (d > 0) ++out.split;
                    total += d;
                }
            }
        }
        out.meanDisagreement = out.births ? total / static_cast<double>(out.births) : 0.0;
        return out;
    };

    const Result off = run(0);
    const Result on = run(sim::kBirthBiasFull);

    // Enough births to measure, and some of them split without the bias — or
    // the comparison below is between two zeroes, which is the mistake this
    // test exists to avoid making twice.
    INFO("without the bias: " << off.births << " births, " << off.split
         << " split, mean disagreement " << off.meanDisagreement);
    INFO("with the bias:    " << on.births << " births, " << on.split
         << " split, mean disagreement " << on.meanDisagreement);
    REQUIRE(off.births > 1000);
    REQUIRE(on.births > 1000);
    REQUIRE(off.split > 100);

    // Births with split parents roughly halve, and the disagreement a birth is
    // exposed to halves with them. A generous margin, because the quantity is a
    // property of the run rather than of one draw.
    CHECK(static_cast<double>(on.split) / static_cast<double>(on.births) <
          0.75 * static_cast<double>(off.split) / static_cast<double>(off.births));
    CHECK(on.meanDisagreement < 0.75 * off.meanDisagreement);
}

TEST_CASE("the birth bias runs on the GPU and agrees with the oracle", "[genome][gpu]") {
    GlContext gl;
    requireGl(gl);

    // The equivalence sweep compares the two paths with the bias on, but a twin
    // that never fires compares identically to one that does. So this asserts
    // the thing the sweep cannot: that turning the bias on *changes* the GPU's
    // answer, and that the changed answer is the oracle's.
    const core::GridSpec spec = spec2d(64, 64);
    const uint32_t a = maskFor({3}, {2, 3});
    const uint32_t b = maskFor({2, 4, 6}, {1, 4, 5, 7});

    auto run = [&](sim::Path path, uint16_t bias) {
        auto made = sim::Simulation::create(spec, lifeLikeGenomeRule(), path, 5u, 11u);
        if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
        sim::Simulation& s = std::get<sim::Simulation>(made);

        sim::GenomeParams gp;
        gp.scheme = sim::Inheritance::Majority;
        gp.threshold = 0;          // no mutation: the bias is the only variable
        gp.birthBias = bias;
        s.setGenome(gp);

        s.fillRandom(std::vector<double>{0.35});
        sim::Pcg32 pick(17);
        for (size_t i = 0; i < spec.cellCount(); ++i) {
            const uint32_t which = pick.unit() < 0.5 ? a : b;
            std::memcpy(s.fieldHost(0).current().data() + i * 4, &which, 4);
        }
        s.commitHost();
        for (int g = 0; g < 120; ++g) s.step();
        s.syncToHost();
        std::vector<uint8_t> cells(s.host().current().begin(), s.host().current().end());
        std::vector<uint8_t> genomes(s.fieldHost(0).current().begin(), s.fieldHost(0).current().end());
        return std::pair{std::move(cells), std::move(genomes)};
    };

    const auto gpuOff = run(sim::Path::Gpu, 0);
    const auto gpuOn  = run(sim::Path::Gpu, sim::kBirthBiasFull);
    const auto cpuOn  = run(sim::Path::Cpu, sim::kBirthBiasFull);

    size_t alive = 0;
    for (uint8_t v : gpuOn.first) if (v) ++alive;
    REQUIRE(alive > 200);              // there was something to compare

    // The generated code ran. Without this the case below would pass against a
    // shader that ignores the uniform entirely.
    CHECK(gpuOn != gpuOff);

    // And it is the same automaton on both paths, states and genomes alike.
    CHECK(cpuOn.first == gpuOn.first);
    CHECK(cpuOn.second == gpuOn.second);
}
