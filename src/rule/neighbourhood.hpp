// Neighbourhood geometry (SPEC §3).
//
// The canonical neighbour order defined here is part of every non-totalistic
// rule's meaning and must be reproduced exactly on both execution paths. Bit i
// of a signature is the i-th offset returned by neighbourOffsets().

#pragma once

#include <cstdint>
#include <vector>

namespace aether::rule {

enum class NeighbourhoodType : uint8_t { Moore, VonNeumann };

struct Neighbourhood {
    NeighbourhoodType type   = NeighbourhoodType::Moore;
    uint8_t           radius = 1;

    bool operator==(const Neighbourhood&) const = default;
};

struct Offset {
    int8_t dx = 0;
    int8_t dy = 0;
    int8_t dz = 0;

    bool operator==(const Offset&) const = default;
};

// Number of neighbours for the given dimensionality and neighbourhood.
// Moore: (2r+1)^d - 1. Von Neumann: cells at Manhattan distance 1..r.
uint32_t neighbourCount(uint8_t dimensions, Neighbourhood nb);

// Offsets in canonical order: lexicographic by (dz, dy, dx) ascending, with
// the origin skipped. Unused axes are always 0.
std::vector<Offset> neighbourOffsets(uint8_t dimensions, Neighbourhood nb);

}  // namespace aether::rule
