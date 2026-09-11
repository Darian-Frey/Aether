#include "rule/neighbourhood.hpp"

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

uint32_t neighbourCount(uint8_t dimensions, Neighbourhood nb) {
    // Closed forms exist (SPEC §3) but enumeration is the single source of
    // truth; the closed forms are checked against it in the tests.
    return static_cast<uint32_t>(neighbourOffsets(dimensions, nb).size());
}

}  // namespace aether::rule
