// Boundary resolution (SPEC §2).
//
// Maps a possibly out-of-range coordinate to an in-range one, or to "read
// state 0". Written once here and once in GLSL; the two must agree on every
// input, including coordinates more than one extent out of range, which a
// radius larger than the grid can produce (AV-005).
//
//   wrap    -1 -> W-1,  W -> 0            (toroidal)
//   zero    any out-of-range -> nullopt   (reads as state 0)
//   mirror  -1 -> 1,    W -> W-2          (reflect about the edge cell's
//                                          centre; no cell is its own
//                                          neighbour; W == 1 maps to 0)

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <optional>

namespace aether::sim {

inline std::optional<uint32_t> resolve(int64_t c, uint32_t extent, rule::Boundary b) {
    const int64_t W = extent;
    if (c >= 0 && c < W) return static_cast<uint32_t>(c);
    switch (b) {
        case rule::Boundary::Wrap: {
            int64_t m = c % W;
            if (m < 0) m += W;
            return static_cast<uint32_t>(m);
        }
        case rule::Boundary::Zero:
            return std::nullopt;
        case rule::Boundary::Mirror: {
            if (W == 1) return 0u;
            const int64_t period = 2 * (W - 1);
            int64_t m = c % period;
            if (m < 0) m += period;
            if (m >= W) m = period - m;
            return static_cast<uint32_t>(m);
        }
    }
    return std::nullopt;
}

}  // namespace aether::sim
