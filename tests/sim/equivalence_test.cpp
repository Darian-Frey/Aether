// CPU/GPU equivalence (F-002, AV-005, AV-007).
//
// Every fixture rule, under every boundary, seeded with activity everywhere
// including the edges and corners, stepped 1000 generations on both paths
// and compared bitwise. If this file is red, nothing else is trustworthy.

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"
#include "sim/inspect.hpp"
#include "sim/resource.hpp"
#include "sim/gpu_step.hpp"
#include "support/evidence.hpp"
#include "support/fields.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <format>
#include <string>
#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

namespace {

constexpr int kGenerations = 1000;

struct Fixture {
    std::string  name;
    rule::RuleIR ir;
    // Only meaningful for a fixture whose rule declares a resource (F-032).
    // These are run-time controls rather than rule text, so they travel beside
    // the IR rather than inside it (D-024).
    sim::ResourceParams resource;
    // The same for a genome (F-033, D-025).
    sim::GenomeParams genome;
};

// Deterministic fill so a failure reproduces exactly. Not the session RNG;
// this is test scaffolding, not sim/.
void fill(core::HostGrid& g, uint16_t states, uint32_t seed, double density) {
    uint32_t s = seed;
    auto next = [&] { s = s * 1664525u + 1013904223u; return s >> 8; };
    for (uint8_t& c : g.current()) {
        const bool on = (next() % 1000) < static_cast<uint32_t>(density * 1000);
        c = on ? static_cast<uint8_t>(1 + next() % (states - 1u)) : 0;
    }
}

rule::RuleIR dsl(const char* src, uint8_t dims = 2) {
    rule::DslContext ctx;
    ctx.dimensions = dims;
    auto r = rule::parseDsl(src, ctx);
    REQUIRE(r);
    return *r.ir;
}

rule::RuleIR randomTable(rule::Kind kind, uint8_t dims, uint16_t states, rule::Neighbourhood nb, uint32_t seed) {
    rule::RuleIR ir;
    ir.dimensions = dims;
    ir.states = states;
    ir.kind = kind;
    ir.neighbourhood = nb;
    const uint32_t N = rule::neighbourCount(dims, nb);
    const auto size = rule::tableSize(kind, states, N);
    REQUIRE(size);
    rule::Table t;
    t.entries.resize(*size);
    uint32_t s = seed;
    for (uint8_t& e : t.entries) { s = s * 1664525u + 1013904223u; e = static_cast<uint8_t>((s >> 8) % states); }
    ir.transition = t;
    return ir;
}

// The same automaton an expression tree rather than a table, so that the
// generated GLSL and the interpreter that mirrors it are both exercised.
rule::RuleIR lifeAsExpression(uint16_t states = 2) {
    rule::RuleIR ir;
    ir.states = states;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Count, 1},                       //  0  n(1)
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},         //  1
        {rule::ExprOp::Eq, 0, 1},                       //  2  n == 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 3},         //  3
        {rule::ExprOp::Eq, 0, 3},                       //  4  n == 3
        {rule::ExprOp::Or, 2, 4},                       //  5
        {rule::ExprOp::Self},                           //  6
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},         //  7
        {rule::ExprOp::Eq, 6, 7},                       //  8  alive
        {rule::ExprOp::And, 8, 5},                      //  9  survives
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},         // 10
        {rule::ExprOp::Eq, 6, 10},                      // 11  dead
        {rule::ExprOp::And, 11, 4},                     // 12  born
        {rule::ExprOp::Or, 9, 12},                      // 13
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},         // 14
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},         // 15
        {rule::ExprOp::Select, 13, 14, 15},             // 16
    };
    ir.transition = e;
    return ir;
}

// next = (self + number of live neighbours) mod states, which uses the
// arithmetic, the modulo guard and the clamp all at once.
rule::RuleIR arithmeticExpression(uint16_t states) {
    rule::RuleIR ir;
    ir.states = states;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Self},                                     // 0
        {rule::ExprOp::Count, 1},                                 // 1
        {rule::ExprOp::Add, 0, 1},                                // 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, states},              // 3
        {rule::ExprOp::Mod, 2, 3},                                // 4
        {rule::ExprOp::Count, 0},                                 // 5
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},                   // 6
        {rule::ExprOp::Div, 4, 6},                                // 7  divide by zero
        {rule::ExprOp::Sub, 4, 7},                                // 8  so this is node 4
        {rule::ExprOp::IntLiteral, 0, 0, 0, 7},                   // 9
        {rule::ExprOp::Gt, 5, 9},                                 // 10 more than seven dead
        {rule::ExprOp::Select, 10, 6, 8},                         // 11
    };
    ir.transition = e;
    return ir;
}

// A multi-field rule (F-031, D-022): two auxiliary fields, one u8 and one f32,
// read at this site and at a neighbour, written by an expression apiece over the
// same neighbourhood the transition reads. It is in this sweep and not only in
// its own file because this is where a divergence between the paths is caught by
// habit rather than by somebody remembering to look.
rule::RuleIR twoFieldExpression() {
    rule::RuleIR ir;
    ir.states = 4;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    // energy' = dead ? energy + n(1) : energy - 2, clamped to the storage.
    rule::Field energy;
    energy.name = "energy";
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Self},                           // 0
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},         // 1
        {rule::ExprOp::Eq, 0, 1},                       // 2  dead
        {rule::ExprOp::FieldSelf, 0},                   // 3  energy here
        {rule::ExprOp::Count, 1},                       // 4
        {rule::ExprOp::Add, 3, 4},                      // 5
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},         // 6
        {rule::ExprOp::Sub, 3, 6},                      // 7
        {rule::ExprOp::Select, 2, 5, 7},                // 8
    };
    energy.write = e;

    // heat' = (heat + heat east) * 0.5, which decays towards zero and is what
    // found BUG-021: it reaches the subnormal range and stays there.
    rule::Field heat;
    heat.name = "heat";
    heat.cell_type = core::CellType::F32;
    rule::Expression h;
    h.nodes = {
        {rule::ExprOp::FieldSelf, 1},                       // 0
        {rule::ExprOp::FieldNeighbour, 1, 0},               // 1
        {rule::ExprOp::Add, 0, 1},                          // 2
        {rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f},     // 3
        {rule::ExprOp::Mul, 2, 3},                          // 4
    };
    heat.write = h;

    // A third field nothing writes, so the copy-forward is under the sweep too.
    rule::Field carried;
    carried.name = "carried";

    ir.fields = {energy, heat, carried};

    // The state turns on where the energy is high, so the fields feed back into
    // the grid and a divergence in either shows up in both.
    rule::Expression t;
    t.nodes = {
        {rule::ExprOp::FieldSelf, 0},                   // 0
        {rule::ExprOp::IntLiteral, 0, 0, 0, 40},        // 1
        {rule::ExprOp::Gt, 0, 1},                       // 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},         // 3
        {rule::ExprOp::Self},                           // 4
        {rule::ExprOp::Select, 2, 3, 4},                // 5
    };
    ir.transition = t;
    return ir;
}

// A rule built out of the bitwise operators, including the genome's reference
// case: `(bits >> count) & 1`. Here because the five operators are twins across
// rule/glsl.cpp and cpu_step.cpp, and the agreement rules they carry — a logical
// right shift, and a count masked to five bits — are exactly the sort that look
// the same on both sides until a value with its top bit set arrives (D-025).
rule::RuleIR bitwiseExpression() {
    rule::RuleIR ir;
    ir.states = 2;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    rule::Field bits;
    bits.name = "bits";
    bits.cell_type = core::CellType::U32;
    ir.fields = {bits};
    // Deliberately *not* declared as a genome. This fixture is here for the five
    // bitwise operators and for a u32 field's width, and a genome would bring
    // inheritance with it — which the shader does not do until F-033's step 3, so
    // the comparison would be of a path that inherits against one that does not.
    // The genome fixture joins this sweep when the twin exists.

    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::FieldSelf, 0},                    //  0  the bit pattern
        {rule::ExprOp::Count, 1},                        //  1  live neighbours
        {rule::ExprOp::Self},                            //  2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},          //  3
        {rule::ExprOp::Eq, 2, 3},                        //  4  alive
        {rule::ExprOp::IntLiteral, 0, 0, 0, 9},          //  5
        {rule::ExprOp::Add, 1, 5},                       //  6  survival half
        {rule::ExprOp::Select, 4, 6, 1},                 //  7  which nine bits
        {rule::ExprOp::Shr, 0, 7},                       //  8  logical shift
        {rule::ExprOp::BitAnd, 8, 3},                    //  9  & 1
        // The operators the reference case does not need, so that they are
        // exercised too rather than merely compiled. A shift count well past 31
        // is deliberate: both sides mask it, and nothing else would make them.
        {rule::ExprOp::IntLiteral, 0, 0, 0, 40},         // 10
        {rule::ExprOp::Shl, 0, 10},                      // 11  masked to 8
        {rule::ExprOp::BitXor, 11, 0},                   // 12
        {rule::ExprOp::BitOr, 12, 9},                    // 13
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},          // 14
        {rule::ExprOp::BitAnd, 13, 14},                  // 15
    };
    ir.transition = e;
    return ir;
}

// A genome rule (F-033, D-025): the engine derives a newly born cell's bits from
// its live neighbours and the rule reads them to decide. In this sweep because
// inheritance is a twin — `sim/genome` and the GLSL `sim/gpu_step` generates —
// and because it is the only one of the three mutation controls whose arithmetic
// runs per *bit* per *parent*, which is a lot of places for two sides to differ.
// Crossover rather than majority, so the parent draws are exercised too: majority
// alone would agree even if the hashes did not.
rule::RuleIR genomeExpression() {
    rule::RuleIR ir;
    ir.states = 2;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    rule::Field gene;
    gene.name = "genome";
    gene.cell_type = core::CellType::U32;
    ir.fields = {gene};
    ir.genome = rule::Genome{0, 18};

    // (genome >> (alive ? count + 9 : count)) & 1 — the Life-like convention,
    // which is this rule's and not the engine's.
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::FieldSelf, 0},                  // 0
        {rule::ExprOp::Count, 1},                      // 1
        {rule::ExprOp::Self},                          // 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},        // 3
        {rule::ExprOp::Eq, 2, 3},                      // 4
        {rule::ExprOp::IntLiteral, 0, 0, 0, 9},        // 5
        {rule::ExprOp::Add, 1, 5},                     // 6
        {rule::ExprOp::Select, 4, 6, 1},               // 7
        {rule::ExprOp::Shr, 0, 7},                     // 8
        {rule::ExprOp::BitAnd, 8, 3},                  // 9
    };
    ir.transition = e;
    return ir;
}

// A resource rule (F-032, D-024): the engine regenerates one field toward another
// and the rule's write on it is only the draw-down. In this sweep because the
// engine's arithmetic and the GLSL it generates are twins, and a twin that is only
// compared in its own test file is compared when somebody remembers to.
rule::RuleIR resourceExpression() {
    rule::RuleIR ir;
    ir.states = 2;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    rule::Field store;
    store.name = "food";
    store.cell_type = core::CellType::F32;
    rule::Expression draw;
    draw.nodes = {
        {rule::ExprOp::Self},                                // 0
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},              // 1
        {rule::ExprOp::Eq, 0, 1},                            // 2  alive
        {rule::ExprOp::FieldSelf, 0},                        // 3
        // 0.98, not 0.8. A draw-down of a fifth a generation against a 4%
        // regrowth settles at 0.17 of capacity, which is below this rule's own
        // 0.35 feeding threshold whatever the capacity — so the fixture went
        // extinct on its first generation and compared two empty grids for as
        // long as it has existed (IMP-011). At 0.98 it settles at 0.68 of
        // capacity and about half the grid can sustain life. The same arithmetic
        // is in LUA.md and in rules/grazing.lua's header, which is where this
        // should have been looked up rather than guessed.
        {rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.98f},     // 4
        {rule::ExprOp::Mul, 3, 4},                           // 5  eats two percent
        {rule::ExprOp::Select, 2, 5, 3},                     // 6
    };
    store.write = draw;

    rule::Field capacity;
    capacity.name = "fertility";
    capacity.cell_type = core::CellType::F32;

    ir.fields = {store, capacity};
    ir.resource = rule::Resource{0, 1};

    // Alive where there is food and a neighbour, so the population tracks the
    // resource and a divergence in either shows up in both.
    rule::Expression t;
    t.nodes = {
        {rule::ExprOp::FieldSelf, 0},                        // 0
        {rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.35f},     // 1
        {rule::ExprOp::Gt, 0, 1},                            // 2  fed
        {rule::ExprOp::Count, 1},                            // 3
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},              // 4
        {rule::ExprOp::Gt, 3, 4},                            // 5  has company
        {rule::ExprOp::Self},                                // 6
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},              // 7
        {rule::ExprOp::Eq, 6, 7},                            // 8  already alive
        {rule::ExprOp::Or, 5, 8},                            // 9
        {rule::ExprOp::And, 2, 9},                           // 10
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},              // 11
        {rule::ExprOp::Select, 10, 7, 11},                   // 12
    };
    ir.transition = t;
    return ir;
}

// A rule that reads its neighbours by position rather than by count.
rule::RuleIR shiftExpression() {
    rule::RuleIR ir;
    ir.states = 3;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {{rule::ExprOp::Neighbour, 0}};
    ir.transition = e;
    return ir;
}

std::vector<Fixture> fixtures() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"Life B3/S23", dsl("B3/S23")});
    out.push_back({"HighLife B36/S23", dsl("B36/S23")});
    out.push_back({"Brian's Brain B2/S/C3", dsl("B2/S/C3")});
    out.push_back({"Generations B2/S23/C5", dsl("B2/S23/C5")});
    out.push_back({"Cyclic CA, 8 states", dsl(
        "states 8; neighbourhood moore 1;"
        "0: n(1) >= 1 -> 1; 1: n(2) >= 1 -> 2; 2: n(3) >= 1 -> 3; 3: n(4) >= 1 -> 4;"
        "4: n(5) >= 1 -> 5; 5: n(6) >= 1 -> 6; 6: n(7) >= 1 -> 7; 7: n(0) >= 1 -> 0;")});
    out.push_back({"Wireworld", dsl(
        "states 4; neighbourhood moore 1;"
        "1: n(0) >= 0 -> 2; 2: n(0) >= 0 -> 3; 3: n(1) == 1 or n(1) == 2 -> 1;")});
    out.push_back({"Random non-totalistic, 2 states Moore", randomTable(Kind::NonTotalistic, 2, 2, {NeighbourhoodType::Moore, 1}, 7)});
    out.push_back({"Random non-totalistic, 3 states von Neumann", randomTable(Kind::NonTotalistic, 2, 3, {NeighbourhoodType::VonNeumann, 1}, 11)});
    out.push_back({"Random totalistic, 4 states Moore r=2", randomTable(Kind::Totalistic, 2, 4, {NeighbourhoodType::Moore, 2}, 13)});
    out.push_back({"Random outer-totalistic, 5 states von Neumann r=2", randomTable(Kind::OuterTotalistic, 2, 5, {NeighbourhoodType::VonNeumann, 2}, 17)});
    out.push_back({"Hex Life-like B2/S34", dsl("states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 3 or n(1) > 4 -> 0;")});
    out.push_back({"Signature rule written with rot", dsl(
        "states 3; neighbourhood von_neumann 1;"
        "0: [1, _, _, _] rot -> 1; 1: [_, _, _, _] -> 2; 2: [_, _, _, _] -> 0;")});
    out.push_back({"Life as an expression (codegen)", lifeAsExpression()});
    out.push_back({"Arithmetic expression, 5 states (codegen)", arithmeticExpression(5)});
    out.push_back({"Neighbour-indexed expression (codegen)", shiftExpression()});
    out.push_back({"Two fields, u8 and f32 (codegen)", twoFieldExpression()});
    out.push_back({"Bitwise over a u32 genome field", bitwiseExpression()});
    {
        sim::GenomeParams gp;
        gp.scheme = sim::Inheritance::Crossover;
        gp.threshold = sim::mutationThreshold(0.01);
        gp.blockShift = 1;
        out.push_back({"Genome with crossover and per-bit mutation", genomeExpression(), {}, gp});
    }
    {
        // Similarity-biased birth (F-035). Its own fixture rather than a flag on
        // the one above, because the bias is a *second* decision over the same
        // gathered parents and a case with it off proves nothing about it. At
        // full strength so that a divergence in the measure shows as a different
        // grid rather than as a handful of cells; the arithmetic is integer, so
        // the two paths agree exactly or not at all.
        sim::GenomeParams gp;
        gp.scheme = sim::Inheritance::Crossover;
        gp.threshold = sim::mutationThreshold(0.02);
        gp.birthBias = sim::kBirthBiasFull;
        out.push_back({"Genome with similarity-biased birth", genomeExpression(), {}, gp});
    }
    {
        // Half strength and majority inheritance, so the bias is exercised where
        // it only *sometimes* refuses — the regime an integer rounding difference
        // between the twins would hide in, since at full strength a split
        // neighbourhood is refused whatever the rounding.
        sim::GenomeParams gp;
        gp.scheme = sim::Inheritance::Majority;
        gp.threshold = sim::mutationThreshold(0.01);
        gp.birthBias = sim::kBirthBiasFull / 2;
        out.push_back({"Genome with a half-strength birth bias", genomeExpression(), {}, gp});
    }
    {
        sim::ResourceParams rp;
        rp.regen = 0.04f;
        rp.minSeed = 0.001f;
        rp.diffusion = 0.2f;
        out.push_back({"Resource with regeneration and diffusion", resourceExpression(), rp});
    }
    out.push_back({"Life with a 4-state ageing tail", dsl("states 2; neighbourhood moore 1; decay 4; 0: n(1) == 3 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;")});
    // A hard lifespan over a rule that keeps making new cells (F-034). A fixture
    // over Life would have compared two nearly empty grids: its long-term
    // population is still lifes and oscillators, every one of which persists
    // without reproducing, so a deadline kills them and nothing replaces them.
    //
    // Read the rule carefully, because its name is not what it does (BUG-027).
    // A cell matching nothing *keeps its state*, so the second statement — a
    // live cell with 2 or 3 neighbours becomes 1 — is a no-op, and the base is
    // "born on 2, never dying" rather than B2/S23. The deadline is the only
    // thing that kills anything here. It is a perfectly good fixture for
    // comparing the two paths on a lifespan rule and a bad one to quote figures
    // from, which is what was done with it; the names are left as they are
    // pending a decision on whether to re-base them on a rule that also dies of
    // its neighbours.
    out.push_back({"Born on 2, never dying, with an 8-generation lifespan", dsl(
        "states 2; neighbourhood moore 1; lifespan 8;"
        "0: n(1) == 2 -> 1; 1: n(1) == 2 or n(1) == 3 -> 1;")});
    out.push_back({"Lifespan and an ageing tail together (the same base)", dsl(
        "states 2; neighbourhood moore 1; lifespan 5; decay 3;"
        "0: n(1) == 2 -> 1; 1: n(1) == 2 or n(1) == 3 -> 1;")});
    out.push_back({"Random non-totalistic hex, 2 states", randomTable(Kind::NonTotalistic, 2, 2, {NeighbourhoodType::Hexagonal, 1}, 29)});
    out.push_back({"Random outer-totalistic hex r=2, 3 states", randomTable(Kind::OuterTotalistic, 2, 3, {NeighbourhoodType::Hexagonal, 2}, 31)});
    return out;
}

// The rule Phase 4 was specified against: three-dimensional, Moore, and
// depending on individual neighbours rather than counts, so its table would
// be 2^26 entries per state and only generated code can run it.
rule::RuleIR nonTotalistic3dExpression() {
    rule::RuleIR ir;
    ir.dimensions = 3;
    ir.states = 2;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};   // 26 neighbours
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Neighbour, 0},                     // 0  a corner
        {rule::ExprOp::Neighbour, 12},                    // 1  a face
        {rule::ExprOp::Neighbour, 25},                    // 2  the opposite corner
        {rule::ExprOp::Add, 0, 1},                        // 3
        {rule::ExprOp::Add, 3, 2},                        // 4
        {rule::ExprOp::Self},                             // 5
        {rule::ExprOp::Add, 4, 5},                        // 6
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},           // 7
        {rule::ExprOp::Mod, 6, 7},                        // 8  parity of four cells
    };
    ir.transition = e;
    return ir;
}

std::vector<Fixture> fixtures3d() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"3D B5/S45 Moore", dsl("B5/S45", 3)});
    out.push_back({"3D von Neumann 3 states", dsl(
        "states 3; neighbourhood von_neumann 1;"
        "0: n(1) == 2 -> 1; 1: n(1) >= 0 -> 2; 2: n(1) >= 0 -> 0;", 3)});
    out.push_back({"3D Moore non-totalistic expression (codegen)", nonTotalistic3dExpression()});
    out.push_back({"3D random non-totalistic von Neumann", randomTable(Kind::NonTotalistic, 3, 2, {NeighbourhoodType::VonNeumann, 1}, 19)});
    return out;
}

std::vector<Fixture> fixtures1d() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"1D random non-totalistic r=3, 3 states", randomTable(Kind::NonTotalistic, 1, 3, {NeighbourhoodType::Moore, 3}, 23)});
    out.push_back({"1D B1/S", dsl("B1/S", 1)});
    return out;
}

// What the CPU path makes of one cell. This is the standalone reason IMP-005
// was worth doing: a failing case used to name a cell index and stop there.
std::string explainCell(const rule::CompiledRule& r, const core::GridSpec& spec,
                        std::span<const uint8_t> cells, size_t linear,
                        uint64_t generation, sim::CellMutation mutation,
                        sim::FieldReads fields = {}) {
    // `stepCell` wants a buffer pair per declared field and a Release build does
    // not check, so a diagnostic called without them reads past the end of
    // nothing — which is BUG-022 again, and worse here: the crash replaces the
    // mismatch report that would have said what was actually wrong.
    if (r.fields.size() != fields.size()) {
        return std::format("(no explanation: this rule declares {} field(s) and the diagnostic was given {})",
                           r.fields.size(), fields.size());
    }
    const uint32_t x = static_cast<uint32_t>(linear % spec.width);
    const uint32_t y = static_cast<uint32_t>((linear / spec.width) % spec.height);
    const uint32_t z = static_cast<uint32_t>(linear / (size_t{spec.width} * spec.height));
    sim::StepScratch scratch(r);
    const sim::CellTransition t = sim::stepCell(r, spec, cells, x, y, z, generation, mutation, scratch, fields);

    std::string nbrs;
    for (uint32_t i = 0; i < r.neighbourCount(); ++i) {
        nbrs += std::format("{}{}", i ? "," : "", static_cast<int>(scratch.neighbours[i]));
    }
    // An axis of extent 1 has no edges; without that, every cell of a 2D grid
    // reports as being on one, since z is both 0 and depth - 1.
    const bool edge = (spec.width  > 1 && (x == 0 || x + 1 == spec.width)) ||
                      (spec.height > 1 && (y == 0 || y + 1 == spec.height)) ||
                      (spec.depth  > 1 && (z == 0 || z + 1 == spec.depth));
    return std::format("({},{},{}){}, state {}, neighbours [{}] -> {} {}{}",
                       x, y, z, edge ? " on an edge" : "", static_cast<int>(t.own), nbrs,
                       static_cast<int>(t.fromRule),
                       t.hasIndex ? std::format("from table entry {}", t.tableIndex) : "from the expression",
                       t.mutated ? std::format(", then mutated to {}", static_cast<int>(t.next)) : "");
}

// Runs one fixture under one boundary on both paths and compares.
void checkEquivalence(const Fixture& f, rule::Boundary boundary, const core::GridSpec& spec, double p = 0.0,
                      uint8_t blockShift = 0) {
    rule::RuleIR ir = f.ir;
    ir.boundary = boundary;
    auto compiled = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
    const rule::CompiledRule& lut = std::get<rule::CompiledRule>(compiled);

    core::HostGrid host(spec);
    fill(host, ir.states, 0x5eed + static_cast<uint32_t>(boundary), 0.4);
    // Kept so the comparison can be checked for having compared anything.
    const std::vector<uint8_t> seed(host.current().begin(), host.current().end());

    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    // Auxiliary fields, seeded the same on both sides (F-031). Empty for every
    // fixture but the multi-field one, which is why the loops below cost nothing
    // for the rest.
    aether::test::HostFields fields(lut, spec.cellCount());
    uint32_t fs = 0xf1e1d + static_cast<uint32_t>(boundary);
    auto nextByte = [&] { fs = fs * 1664525u + 1013904223u; return fs >> 8; };
    for (size_t f = 0; f < fields.size(); ++f) {
        for (uint64_t i = 0; i < spec.cellCount(); ++i) {
            switch (fields.type(f)) {
                case core::CellType::F32:
                    fields.setF32(f, i, static_cast<float>(nextByte() % 1000) / 1000.0f);
                    break;
                case core::CellType::U32:
                    // A genome's whole point is a bit pattern, so seed one:
                    // eighteen bits, which is what a Life-like mask occupies.
                    fields.setU32(f, i, nextByte() & 0x3ffffu);
                    break;
                case core::CellType::U8:
                    fields.setU8(f, i, static_cast<uint8_t>(nextByte() % 256));
                    break;
            }
        }
    }
    auto madeFields = aether::test::GpuFields::create(lut, spec);
    if (const auto* e = std::get_if<core::Error>(&madeFields)) FAIL(f.name + ": " + e->message);
    aether::test::GpuFields& fieldGpu = std::get<aether::test::GpuFields>(madeFields);
    fieldGpu.upload(fields);

    sim::GpuStepper stepper;
    const auto err = stepper.setRule(lut, spec);
    if (err) FAIL(err->message);
    const sim::CellMutation mutation{sim::mutationThreshold(p), 0xb0b0b0b0ull + static_cast<uint32_t>(boundary), blockShift};
    stepper.setCellMutation(mutation);
    // The engine's own half of the resource, on both paths. A combination this
    // boundary cannot honour is skipped rather than compared: diffusion against a
    // mirror boundary creates material and is refused by the engine, so there is
    // nothing to agree about.
    sim::ResourceParams resource = f.resource;
    if (sim::resourceProblem(resource, boundary) != nullptr) resource.diffusion = 0.0f;
    stepper.setResource(resource);
    // Inheritance, on both paths. The seed is the mutation's, as `Simulation`
    // makes it: one seed governs the whole of stream B.
    sim::GenomeParams genomeParams = f.genome;
    genomeParams.seedB = mutation.seedB;
    stepper.setGenome(genomeParams);

    // The comparison is made at the last generation the CPU run still held
    // something, rather than always at 1000 (IMP-011). A rule that dies at
    // generation 9 under a zero boundary used to be compared at 1000, which was
    // a comparison of two empty grids: green, fast and worthless. Found rather
    // than configured, because which generation that is depends on the rule *and*
    // the boundary — `3D B5/S45` runs the full thousand under wrap and dies at
    // nine under zero — so a number written into each fixture would have been a
    // number per fixture per boundary, and wrong the first time a rule changed.
    //
    // The cost is one grid copy per generation while the grid is alive, which is
    // a few kilobytes against a step over the whole thing.
    std::vector<uint8_t> lastLiving(host.current().begin(), host.current().end());
    int lastLivingAt = 0;
    auto remember = [&](int generation) {
        for (uint8_t b : host.current()) {
            if (b != 0) {
                std::copy(host.current().begin(), host.current().end(), lastLiving.begin());
                lastLivingAt = generation;
                return;
            }
        }
    };

    // The CPU pass first, all the way, remembering where it last held something.
    for (int i = 0; i < kGenerations; ++i) {
        if (fields.size() == 0) {
            sim::cpuStep(lut, host, static_cast<uint64_t>(i), mutation);
        } else {
            sim::cpuStep(lut, spec, host.current(), host.next(), static_cast<uint64_t>(i), mutation,
                         fields.reads(), fields.writes(), resource, nullptr, genomeParams);
            host.swap();
            fields.swap();
        }
        remember(i + 1);
    }

    // A fixture whose rule is dead on arrival is a broken fixture, not a world
    // that happened to end: there is no generation at which the two paths could
    // have been compared on anything.
    if (lastLivingAt == 0) {
        FAIL(std::format("{} / {}: the grid was empty after one generation, so there is no "
                         "generation at which this fixture compares anything (IMP-011)",
                         f.name, rule::toString(boundary)));
    }

    // Then the GPU, to exactly that generation.
    for (int i = 0; i < lastLivingAt; ++i) {
        if (fields.size() == 0) {
            stepper.step(gpu);
        } else {
            stepper.step(gpu.current(), gpu.next(), fieldGpu.textures());
            gpu.swap();
            fieldGpu.swap();
        }
    }

    std::vector<uint8_t> fromGpu(spec.bytesPerBuffer());
    gpu.download(fromGpu);
    const std::vector<uint8_t>& fromCpu = lastLiving;

    size_t firstDiff = fromCpu.size();
    for (size_t i = 0; i < fromCpu.size(); ++i) {
        if (fromCpu[i] != fromGpu[i]) { firstDiff = i; break; }
    }
    std::string detail;
    if (firstDiff != fromCpu.size()) {
        // The grids are compared after `lastLivingAt` generations, so this cell is
        // where the divergence had reached, not necessarily where it began.
        detail = std::format("\n  cpu {} vs gpu {}\n  the CPU path reads that cell as {}",
                             static_cast<int>(fromCpu[firstDiff]), static_cast<int>(fromGpu[firstDiff]),
                             explainCell(lut, spec, fromCpu, firstDiff, lastLivingAt, mutation,
                                         fields.reads()));
    }
    INFO(std::format("{} / {} / {}x{}x{} / p={} / compared at generation {}: "
                     "first difference at cell {}{}",
                     f.name, rule::toString(boundary), spec.width, spec.height, spec.depth, p,
                     lastLivingAt, firstDiff, detail));
    CHECK(firstDiff == fromCpu.size());

    // And the comparison was of something: not empty, by construction above, and
    // not the seed either — a grid that never moved means the step was never
    // really exercised (IMP-011).
    if (auto why = aether::test::comparisonEvidence(seed, fromCpu)) {
        FAIL(std::format("{} / {} at generation {}: {}", f.name, rule::toString(boundary),
                         lastLivingAt, *why));
    }

    // The fields on the same terms. A sweep that compared the state alone would
    // pass while the rule's own bookkeeping had diverged, and a field feeds the
    // next generation's state, so that is a difference waiting rather than one
    // avoided.
    for (size_t fi = 0; fi < fields.size(); ++fi) {
        std::vector<uint8_t> got(fields.raw(fi).size());
        fieldGpu.download(fi, got);
        size_t diff = got.size();
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] != fields.raw(fi)[i]) { diff = i; break; }
        }
        const size_t cell = diff == got.size() ? diff : diff / core::cellBytes(fields.type(fi));
        INFO(std::format("{} / {} / field {} ({}): first difference at cell {}", f.name,
                         rule::toString(boundary), fi, core::toString(fields.type(fi)), cell));
        CHECK(diff == got.size());
    }
}

}  // namespace

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 2D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            // Extents chosen not to be multiples of the workgroup size.
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 3D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 1D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, core::GridSpec{1, 131, 1, 1});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise with cell mutation on, all dimensions", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1}, 0.02);
        }
    }
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11}, 0.02);
        }
    }
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{1, 131, 1, 1}, 0.02);
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise with block-correlated mutation", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02 block=4") {
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1}, 0.02, 2);
        }
    }
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02 block=2") {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11}, 0.02, 1);
        }
    }
}

TEST_CASE("cell mutation changes the trajectory and is reproducible", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 48, 48, 1};
    const rule::CompiledRule life = std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23")));
    auto run = [&](double p, uint64_t seed) {
        core::HostGrid h(spec);
        fill(h, 2, 77, 0.35);
        const sim::CellMutation m{sim::mutationThreshold(p), seed};
        for (int i = 0; i < 200; ++i) sim::cpuStep(life, h, static_cast<uint64_t>(i), m);
        return std::vector<uint8_t>(h.current().begin(), h.current().end());
    };
    const auto plain = run(0.0, 1);
    const auto mutA  = run(0.01, 1);
    const auto mutA2 = run(0.01, 1);
    const auto mutB  = run(0.01, 2);
    CHECK(plain != mutA);
    CHECK(mutA == mutA2);
    CHECK(mutA != mutB);
}

TEST_CASE("the two backends agree on a rule expressible both ways (AV-007)", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    const core::GridSpec spec{2, 61, 43, 1};

    // Conway's Life as a table and as an expression tree. Which backend runs
    // a rule must not change what the rule does, whatever LUT_MAX_ENTRIES
    // happens to be.
    rule::RuleIR table = dsl("B3/S23");
    rule::RuleIR expression = lifeAsExpression();
    table.boundary = expression.boundary = boundary;
    REQUIRE(rule::selectBackend(table) == rule::Backend::Lut);
    REQUIRE(rule::selectBackend(expression) == rule::Backend::Codegen);

    auto run = [&](const rule::RuleIR& ir, bool onCpu) {
        auto compiled = rule::compileRule(ir);
        if (const auto* e = std::get_if<rule::CompileError>(&compiled)) FAIL(e->message);
        const rule::CompiledRule& rule = std::get<rule::CompiledRule>(compiled);

        core::HostGrid host(spec);
        fill(host, 2, 0x11ce, 0.4);
        auto made = core::GpuGrid::create(spec, core::queryVram());
        REQUIRE(std::holds_alternative<core::GpuGrid>(made));
        core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
        gpu.upload(host.current());

        sim::GpuStepper stepper;
        if (auto e = stepper.setRule(rule, spec)) FAIL(e->message);
        for (int i = 0; i < kGenerations; ++i) {
            if (onCpu) sim::cpuStep(rule, host, static_cast<uint64_t>(i), {});
            else stepper.step(gpu);
        }
        std::vector<uint8_t> out(spec.bytesPerBuffer());
        if (onCpu) std::copy(host.current().begin(), host.current().end(), out.begin());
        else gpu.download(out);
        return out;
    };

    const auto tableGpu = run(table, false);
    CHECK(run(expression, false) == tableGpu);
    CHECK(run(expression, true) == tableGpu);
    CHECK(run(table, true) == tableGpu);
}

TEST_CASE("a generated rule is compiled once per rule, a table rule once per shape", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;

    // Two table rules of one shape share a program: mutation is an upload.
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B36/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);

    // A generated rule is its program, so a different one compiles again...
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(lifeAsExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 2);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(shiftExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 3);
    // ...and the same one does not (SPEC §6: the cache is keyed on ir_hash).
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(lifeAsExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 3);
}

TEST_CASE("GPU glider arrives at its predicted offset (AV-004 detector)", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 32, 32, 1};
    core::HostGrid host(spec);
    for (auto [x, y] : {std::pair{1u, 0u}, {2u, 1u}, {0u, 2u}, {1u, 2u}, {2u, 2u}}) host.set(x, y, 0, 1);

    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    const rule::CompiledRule life = std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23")));
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(life, spec).has_value());
    for (int i = 0; i < 100; ++i) stepper.step(gpu);
    CHECK(stepper.generation() == 100);

    std::vector<uint8_t> out(spec.cellCount());
    gpu.download(out);
    core::HostGrid expected(spec);
    for (auto [x, y] : {std::pair{1u, 0u}, {2u, 1u}, {0u, 2u}, {1u, 2u}, {2u, 2u}}) expected.set((x + 25) % 32, (y + 25) % 32, 0, 1);
    CHECK(out == std::vector<uint8_t>(expected.current().begin(), expected.current().end()));
}

TEST_CASE("changing the table without changing the shape compiles nothing", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B36/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B2/S/C3"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 2);
}

TEST_CASE("a rule whose dimensionality mismatches the grid is refused, leaving the old rule active", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    const auto err = stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B5/S45", 3))), spec);
    REQUIRE(err.has_value());
    CHECK(err->message.find("3D") != std::string::npos);
    CHECK(stepper.hasRule());
}

// --- The inspector against the stepper (F-030, AV-017) ----------------------
//
// The inspector is consulted precisely when nobody can check its answer, so
// what it predicts for a cell must be what the stepper writes into that cell.
// Run over the same fixtures under the same boundaries as the equivalence
// cases above, because the cells that matter are the edges and the corners: an
// inspector resolving neighbours differently from `sim::resolve` explains the
// interior perfectly and lies about the rim. No GL, so this is not a [gpu]
// case — it compares the oracle against itself.

namespace {

void checkInspector(const Fixture& f, rule::Boundary boundary, const core::GridSpec& spec) {
    // State-only, for the reason inspect.hpp gives: the pad it reads holds one
    // grid, so a rule with fields is not describable here (BUG-022).
    if (!f.ir.fields.empty()) return;
    rule::RuleIR ir = f.ir;
    ir.boundary = boundary;
    auto compiled = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(compiled));

    core::HostGrid grid(spec);
    fill(grid, ir.states, 4242u, 0.4);

    // What the stepper writes, for every cell at once.
    sim::cpuStep(rule, spec, grid.current(), grid.next(), 0, sim::CellMutation{});

    sim::StepScratch scratch(rule);
    size_t checked = 0;
    for (uint32_t z = 0; z < spec.depth; ++z) {
        for (uint32_t y = 0; y < spec.height; ++y) {
            for (uint32_t x = 0; x < spec.width; ++x) {
                const sim::Inspection got =
                    sim::inspect(rule, spec, grid.current(), x, y, z, 0, sim::CellMutation{}, scratch);
                const size_t at = (size_t{z} * spec.height + y) * spec.width + x;
                if (got.transition.next != grid.next()[at]) {
                    INFO(f.name << " / " << rule::toString(boundary) << " at " << x << "," << y << "," << z);
                    CHECK(got.transition.next == grid.next()[at]);
                    return;
                }
                // The neighbours it reports must be the ones the rule was
                // given, in the order it was given them.
                REQUIRE(got.neighbours.size() == rule.neighbourCount());
                ++checked;
            }
        }
    }
    CHECK(checked == spec.cellCount());
}

}  // namespace

TEST_CASE("the inspector predicts what the stepper writes, 2D", "[inspect][equivalence]") {
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{2, 37, 29, 1});
        }
    }
}

TEST_CASE("the inspector predicts what the stepper writes, 3D and 1D", "[inspect][equivalence]") {
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{3, 11, 9, 7});
        }
    }
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{1, 71, 1, 1});
        }
    }
}

TEST_CASE("the inspector predicts what the stepper writes with mutation on", "[inspect][equivalence]") {
    // Mutation is hashed from the coordinate and the generation, so the
    // inspector must agree about an overridden cell too — and say that it was
    // overridden rather than reporting the rule's answer as the outcome.
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero);
    const core::GridSpec spec{2, 37, 29, 1};
    for (const Fixture& f : fixtures()) {
        // The inspector is state-only: it reads the transition out of stepCell,
        // which wants a buffer pair per declared field, and its caller is the
        // editor's pad, which holds one grid (BUG-022).
        if (!f.ir.fields.empty()) continue;
        rule::RuleIR ir = f.ir;
        ir.boundary = boundary;
        auto compiled = rule::compileRule(ir);
        REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
        const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(compiled));

        sim::CellMutation mutation;
        mutation.threshold = sim::mutationThreshold(0.05);
        mutation.seedB = 99;

        core::HostGrid grid(spec);
        fill(grid, ir.states, 777u, 0.4);
        sim::cpuStep(rule, spec, grid.current(), grid.next(), 5, mutation);

        sim::StepScratch scratch(rule);
        size_t overridden = 0;
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.05") {
            for (uint32_t y = 0; y < spec.height; ++y) {
                for (uint32_t x = 0; x < spec.width; ++x) {
                    const sim::Inspection got =
                        sim::inspect(rule, spec, grid.current(), x, y, 0, 5, mutation, scratch);
                    REQUIRE(got.transition.next == grid.next()[size_t{y} * spec.width + x]);
                    if (got.transition.mutated) {
                        ++overridden;
                        // What the rule alone would have given is still there
                        // to show beside what mutation did with it. The two
                        // may coincide: cell mutation draws uniformly over
                        // every state and does not exclude the current one
                        // (SPEC §9.2), so the check is that `fromRule` is
                        // the rule's answer, not that it differs.
                        const sim::Inspection unmutated =
                            sim::inspect(rule, spec, grid.current(), x, y, 0, 5, sim::CellMutation{}, scratch);
                        CHECK_FALSE(unmutated.transition.mutated);
                        CHECK(got.transition.fromRule == unmutated.transition.next);
                    }
                }
            }
            CHECK(overridden > 0);
        }
    }
}

// --- The bundled library as the fixture set (F-002) --------------------------
//
// F-002's acceptance is bit-identical grids "for every rule in the bundled
// library", and until now the suite proved it for fifteen fixtures written to
// exercise the table kinds instead. Those are the better test of the index
// arithmetic; these are the better test of what somebody actually runs. A
// bundled rule that stepped differently on the two paths would be shipped,
// named in the Library panel, and wrong — and nothing here would have said so.
//
// Continuous rules are excluded and covered by `continuous_test.cpp`: this
// harness seeds a grid by writing bytes, which on an f32 grid writes into the
// middle of values rather than producing them.

namespace {

std::vector<Fixture> libraryFixtures() {
    std::vector<Fixture> out;
    for (const rule::LibraryRule& entry : rule::loadLibrary({AETHER_RULES_DIR})) {
        auto built = rule::compileLibraryRule(entry, rule::Boundary::Wrap);
        if (const auto* e = std::get_if<std::string>(&built)) {
            FAIL("bundled rule " + entry.id + ": " + *e);
        }
        rule::RuleIR ir = std::get<rule::RuleIR>(std::move(built));
        if (ir.cell_type == core::CellType::F32) continue;
        out.push_back({entry.id, std::move(ir)});
    }
    return out;
}

// Small enough to run every bundled rule in a reasonable time, and none of
// them a multiple of the workgroup size.
core::GridSpec specFor(const rule::RuleIR& ir) {
    switch (ir.dimensions) {
        case 1:  return {1, 131, 1, 1};
        case 3:  return {3, 19, 14, 11};
        default: return {2, 61, 43, 1};
    }
}

}  // namespace

TEST_CASE("every bundled rule steps identically on both paths", "[gpu][equivalence][library]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    const auto fixtures = libraryFixtures();
    REQUIRE(fixtures.size() >= 15);
    for (const Fixture& f : fixtures) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, specFor(f.ir));
        }
    }
}

TEST_CASE("every bundled rule steps identically with cell mutation on", "[gpu][equivalence][library]") {
    GlContext gl;
    requireGl(gl);
    const auto fixtures = libraryFixtures();
    for (const Fixture& f : fixtures) {
        DYNAMIC_SECTION(f.name << " / wrap / p=0.02") {
            checkEquivalence(f, rule::Boundary::Wrap, specFor(f.ir), 0.02);
        }
    }
}

