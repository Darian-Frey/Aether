// Neighbourhood geometry (SPEC §3).
//
// The canonical neighbour order defined here is part of every non-totalistic
// rule's meaning and must be reproduced exactly on both execution paths. Bit i
// of a signature is the i-th offset returned by neighbourOffsets().

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace aether::rule {

// Hexagonal (D-012, F-023): the grid's (x, y) are axial (q, r) coordinates
// on a hex lattice; the six neighbours at radius 1 are (±1,0), (0,±1),
// (1,-1), (-1,1). Radius r covers hex distance <= r, N = 3r(r+1). 2D only
// (1D degenerates to two neighbours); rejected in 3D by validation.
enum class NeighbourhoodType : uint8_t { Moore, VonNeumann, Hexagonal };

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

// Permutation taking each neighbour index to where one rotation sends it: a
// quarter turn on a square lattice, a sixth on a hexagonal one. Rotating a
// pattern `v` gives `v'[perm[i]] = v[i]`. Defined for 2D only — a rotation
// in 3D would have to pick an axis, and in 1D there is none (SPEC §7 `rot`).
std::optional<std::vector<uint32_t>> rotationPermutation(uint8_t dimensions, Neighbourhood nb);

}  // namespace aether::rule
