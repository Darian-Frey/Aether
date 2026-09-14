#include "core/grid.hpp"
#include "rule/dsl.hpp"
#include "rule/lut.hpp"
#include "sim/cpu_step.hpp"
#include "sim/hash.hpp"
#include "sim/shaders.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <rlgl.h>

#include <array>
#include <set>
#include <string>
#include <vector>

using namespace aether::sim;
using aether::test::GlContext;
using aether::test::requireGl;

TEST_CASE("hash32 is deterministic and sensitive to every argument", "[hash]") {
    const uint32_t base = hash32(10, 20, 30, 40, 50);
    CHECK(hash32(10, 20, 30, 40, 50) == base);
    CHECK(hash32(11, 20, 30, 40, 50) != base);
    CHECK(hash32(10, 21, 30, 40, 50) != base);
    CHECK(hash32(10, 20, 31, 40, 50) != base);
    CHECK(hash32(10, 20, 30, 41, 50) != base);
    CHECK(hash32(10, 20, 30, 40, 51) != base);
    CHECK(hash32(10, 20, 30, 40ull + (1ull << 40), 50) != base);   // high generation bits
    CHECK(hash32(10, 20, 30, 40, 50ull + (1ull << 40)) != base);   // high seed bits
    CHECK(hash32(20, 10, 30, 40, 50) != base);                     // order matters
}

TEST_CASE("hash32 is well distributed over a grid", "[hash]") {
    // Bit balance and threshold selection rate over 256x256 cells.
    std::array<uint32_t, 32> ones{};
    uint32_t selected = 0;
    const uint32_t threshold = mutationThreshold(0.05);
    for (uint32_t y = 0; y < 256; ++y) {
        for (uint32_t x = 0; x < 256; ++x) {
            const uint32_t h = hash32(x, y, 0, 7, 12345);
            for (size_t b = 0; b < 32; ++b) ones[b] += (h >> b) & 1u;
            if (h < threshold) ++selected;
        }
    }
    for (uint32_t n : ones) { CHECK(n > 31500); CHECK(n < 34000); }
    CHECK(selected > 2900);   // 5% of 65536 = 3277
    CHECK(selected < 3650);
}

TEST_CASE("uniformState covers the range evenly and mutatedState is independent of the test", "[hash]") {
    std::array<uint32_t, 5> counts{};
    for (uint32_t i = 0; i < 100000; ++i) counts[uniformState(mix32(i), 5)] += 1;
    for (uint32_t n : counts) { CHECK(n > 19000); CHECK(n < 21000); }

    // Among hashes that pass a small threshold, the replacement state must
    // still be spread — the BUG-005 case.
    std::array<uint32_t, 4> mutated{};
    const CellMutation m{mutationThreshold(0.001), 99};
    uint32_t passed = 0;
    for (uint32_t y = 0; y < 1024; ++y) {
        for (uint32_t x = 0; x < 1024; ++x) {
            const uint32_t h = hash32(x, y, 0, 3, m.seedB);
            if (mutates(h, m)) { ++passed; mutated[mutatedState(h, 4)] += 1; }
        }
    }
    CHECK(passed > 800);   // ~1049
    for (uint32_t n : mutated) CHECK(n > passed / 8);
}

TEST_CASE("mutationThreshold endpoints", "[hash]") {
    CHECK(mutationThreshold(0.0) == 0);
    CHECK(mutationThreshold(-1.0) == 0);
    CHECK(mutationThreshold(1.0) == 0xFFFFFFFFu);
    CHECK(mutationThreshold(0.5) == 0x80000000u);
}

TEST_CASE("cell mutation on the CPU path replaces about p of the cells", "[hash]") {
    // B/S: nothing is ever born or survives, so every non-zero cell after one
    // step is a mutation. Half of mutations land on state 0 for S=2.
    const auto ir = *aether::rule::parseDsl("B/S").ir;
    const auto lut = std::get<aether::rule::LutRule>(aether::rule::compileLut(ir));
    aether::core::HostGrid g({2, 512, 512, 1});
    cpuStep(lut, g, 0, CellMutation{mutationThreshold(0.1), 42});
    uint32_t alive = 0;
    for (uint8_t c : g.current()) alive += c;
    CHECK(alive > 12000);   // 5% of 262144 = 13107
    CHECK(alive < 14300);

    // p = 0 leaves the rule's output untouched.
    aether::core::HostGrid z({2, 64, 64, 1});
    z.set(3, 3, 0, 1);
    cpuStep(lut, z, 0, CellMutation{0, 42});
    uint32_t any = 0;
    for (uint8_t c : z.current()) any += c;
    CHECK(any == 0);
}

TEST_CASE("the GLSL hash agrees with the C++ hash over a large sweep", "[gpu][hash]") {
    GlContext gl;
    requireGl(gl);

    // Four (z, generation, seed) combinations over a 512x512 (x, y) plane,
    // with generation and seed values that exercise both 32-bit halves.
    struct Combo { uint32_t z; uint64_t gen; uint64_t seed; };
    const Combo combos[4] = {
        {0, 0, 0},
        {3, 1234567, 0xdeadbeefcafef00dull},
        {17, (1ull << 33) + 5, 42},
        {255, 0xffffffffffffffffull, 0xffffffffffffffffull},
    };
    constexpr uint32_t W = 512, H = 512;

    std::string src = "#version 430\n";
    src += aether::shaders::kHashGlsl;
    src += R"glsl(
layout(local_size_x = 16, local_size_y = 16) in;
layout(std430, binding = 0) readonly buffer In { uint z; uint genLo; uint genHi; uint seedLo; uint seedHi; uint states; };
layout(std430, binding = 1) writeonly buffer Out { uint out_[]; };
void main() {
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= 512u || p.y >= 512u) return;
    uint h = aetherHash32(p.x, p.y, z, genLo, genHi, seedLo, seedHi);
    out_[(p.y * 512u + p.x) * 2u]      = h;
    out_[(p.y * 512u + p.x) * 2u + 1u] = aetherMutatedState(h, states);
}
)glsl";
    const unsigned int shader = rlLoadShader(src.c_str(), RL_COMPUTE_SHADER);
    REQUIRE(shader != 0);
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    REQUIRE(program != 0);

    std::vector<uint32_t> out(W * H * 2);
    const unsigned int outSsbo = rlLoadShaderBuffer(static_cast<unsigned int>(out.size() * 4), nullptr, RL_DYNAMIC_COPY);

    for (const Combo& c : combos) {
        const uint32_t states = 7;
        const uint32_t in[6] = {c.z, static_cast<uint32_t>(c.gen), static_cast<uint32_t>(c.gen >> 32),
                                static_cast<uint32_t>(c.seed), static_cast<uint32_t>(c.seed >> 32), states};
        const unsigned int inSsbo = rlLoadShaderBuffer(sizeof(in), in, RL_STATIC_READ);
        rlEnableShader(program);
        rlBindShaderBuffer(inSsbo, 0);
        rlBindShaderBuffer(outSsbo, 1);
        rlComputeShaderDispatch(W / 16, H / 16, 1);
        rlDisableShader();
        rlReadShaderBuffer(outSsbo, out.data(), static_cast<unsigned int>(out.size() * 4), 0);
        rlUnloadShaderBuffer(inSsbo);

        size_t mismatches = 0;
        for (uint32_t y = 0; y < H && mismatches < 5; ++y) {
            for (uint32_t x = 0; x < W; ++x) {
                const uint32_t h = hash32(x, y, c.z, c.gen, c.seed);
                if (out[(y * W + x) * 2] != h || out[(y * W + x) * 2 + 1] != mutatedState(h, states)) {
                    ++mismatches;
                    if (mismatches <= 3) {
                        INFO("mismatch at (" << x << "," << y << ") z=" << c.z << ": cpu " << h
                             << " gpu " << out[(y * W + x) * 2]);
                        CHECK(false);
                    }
                }
            }
        }
        CHECK(mismatches == 0);
    }
    rlUnloadShaderBuffer(outSsbo);
    rlUnloadShaderProgram(program);
}

TEST_CASE("block shift 0 is exactly the original per-cell behaviour", "[hash]") {
    const CellMutation m{mutationThreshold(0.1), 0xfeedfacedeadbeefull, 0};
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            CHECK(blockHash(x, y, 3, 99, m) == hash32(x, y, 3, 99, m.seedB));
        }
    }
}

TEST_CASE("a block shares one decision and keeps per-cell replacement states", "[hash]") {
    CellMutation m{mutationThreshold(0.25), 4242, 2};   // blocks of 4
    uint32_t blocksSeen = 0, mutatedCells = 0;
    std::array<uint32_t, 4> states{};
    for (uint32_t by = 0; by < 16; ++by) {
        for (uint32_t bx = 0; bx < 16; ++bx) {
            const bool first = mutates(blockHash(bx * 4, by * 4, 0, 7, m), m);
            ++blocksSeen;
            std::set<uint32_t> distinct;
            for (uint32_t dy = 0; dy < 4; ++dy) {
                for (uint32_t dx = 0; dx < 4; ++dx) {
                    const uint32_t x = bx * 4 + dx, y = by * 4 + dy;
                    // Every cell of the block agrees on whether to mutate.
                    CHECK(mutates(blockHash(x, y, 0, 7, m), m) == first);
                    if (first) {
                        ++mutatedCells;
                        const uint32_t st = mutatedState(hash32(x, y, 0, 7, m.seedB), 4);
                        distinct.insert(st);
                        ++states[st];
                    }
                }
            }
            // A mutating block is a burst of noise, not one flat colour.
            if (first) CHECK(distinct.size() > 1);
        }
    }
    CHECK(blocksSeen == 256);
    // p keeps its meaning: about a quarter of all cells change.
    const double fraction = static_cast<double>(mutatedCells) / (64.0 * 64.0);
    CHECK(fraction > 0.18);
    CHECK(fraction < 0.33);
    for (uint32_t n : states) CHECK(n > 0);
}

TEST_CASE("blocks are aligned, so a neighbouring block decides separately", "[hash]") {
    const CellMutation m{mutationThreshold(0.5), 11, 3};   // blocks of 8
    uint32_t differing = 0;
    for (uint32_t b = 0; b < 32; ++b) {
        if (mutates(blockHash(b * 8, 0, 0, 1, m), m) != mutates(blockHash(b * 8 + 8, 0, 0, 1, m), m)) ++differing;
    }
    CHECK(differing > 8);   // adjacent blocks are independent
    CHECK(blockHash(0, 0, 0, 1, m) == blockHash(7, 7, 7, 1, m));
    CHECK(blockHash(0, 0, 0, 1, m) != blockHash(8, 0, 0, 1, m));
}
