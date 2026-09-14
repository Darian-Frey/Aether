#include "rule/neighbourhood.hpp"

#include <algorithm>
#include <cstdlib>

namespace aether::rule {

namespace {

bool isMember(NeighbourhoodType type, int r, int dx, int dy, int dz) {
    if (dx == 0 && dy == 0 && dz == 0) return false;
    switch (type) {
        case NeighbourhoodType::Moore:
            return std::abs(dx) <= r && std::abs(dy) <= r && std::abs(dz) <= r;
        case NeighbourhoodType::VonNeumann:
            return std::abs(dx) + std::abs(dy) + std::abs(dz) <= r;
        case NeighbourhoodType::Hexagonal:
            // Axial hex distance: max(|dq|, |dr|, |dq + dr|).
            return dz == 0 && std::max({std::abs(dx), std::abs(dy), std::abs(dx + dy)}) <= r;
    }
    return false;
}

}  // namespace

std::vector<Offset> neighbourOffsets(uint8_t dimensions, Neighbourhood nb) {
    const int r  = nb.radius;
    const int ry = dimensions >= 2 ? r : 0;
    const int rz = dimensions >= 3 ? r : 0;

    std::vector<Offset> out;
    for (int dz = -rz; dz <= rz; ++dz) {
        for (int dy = -ry; dy <= ry; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (isMember(nb.type, r, dx, dy, dz)) {
                    out.push_back(Offset{static_cast<int8_t>(dx),
                                         static_cast<int8_t>(dy),
                                         static_cast<int8_t>(dz)});
                }
            }
        }
    }
    return out;
}

std::optional<std::vector<uint32_t>> rotationPermutation(uint8_t dimensions, Neighbourhood nb) {
    if (dimensions != 2) return std::nullopt;
    const auto offs = neighbourOffsets(dimensions, nb);
    const bool hex = nb.type == NeighbourhoodType::Hexagonal;
    auto turn = [hex](Offset o) -> Offset {
        // Square: (x, y) -> (-y, x). Hex axial: (q, r) -> (-r, q + r).
        return hex ? Offset{static_cast<int8_t>(-o.dy), static_cast<int8_t>(o.dx + o.dy), 0}
                   : Offset{static_cast<int8_t>(-o.dy), o.dx, 0};
    };
    std::vector<uint32_t> perm(offs.size());
    for (size_t i = 0; i < offs.size(); ++i) {
        const Offset r = turn(offs[i]);
        const auto it = std::find(offs.begin(), offs.end(), r);
        if (it == offs.end()) return std::nullopt;   // not closed under the turn
        perm[i] = static_cast<uint32_t>(it - offs.begin());
    }
    return perm;
}

uint32_t neighbourCount(uint8_t dimensions, Neighbourhood nb) {
    // Closed forms exist (SPEC §3) but enumeration is the single source of
    // truth; the closed forms are checked against it in the tests.
    return static_cast<uint32_t>(neighbourOffsets(dimensions, nb).size());
}

}  // namespace aether::rule
