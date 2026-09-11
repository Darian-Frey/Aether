#include "sim/cpu_step.hpp"

#include "sim/boundary.hpp"

#include <cassert>
#include <vector>

namespace aether::sim {

using rule::Kind;

void cpuStep(const rule::LutRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next) {
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
                        assert(false && "compileLut does not produce these kinds");
                        break;
                }
                next[(size_t{z} * H + y) * W + x] = rule.table[index];
            }
        }
    }
}

void cpuStep(const rule::LutRule& rule, core::HostGrid& grid) {
    cpuStep(rule, grid.spec(), grid.current(), grid.next());
    grid.swap();
}

}  // namespace aether::sim
