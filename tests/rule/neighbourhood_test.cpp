#include "rule/neighbourhood.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <tuple>

using namespace aether::rule;

TEST_CASE("neighbour counts match the SPEC §3 closed forms", "[neighbourhood]") {
    for (uint8_t d = 1; d <= 3; ++d) {
        for (uint8_t r = 1; r <= 3; ++r) {
            const uint32_t side = 2u * r + 1u;
            uint32_t moore = 1;
            for (int i = 0; i < d; ++i) moore *= side;
            moore -= 1;
            CHECK(neighbourCount(d, {NeighbourhoodType::Moore, r}) == moore);

            uint32_t vn = 0;
            switch (d) {
                case 1: vn = 2u * r; break;
                case 2: vn = 2u * r * (r + 1u); break;
                case 3: vn = (2u * r + 1u) * (2u * r * r + 2u * r + 3u) / 3u - 1u; break;
            }
            CHECK(neighbourCount(d, {NeighbourhoodType::VonNeumann, r}) == vn);
        }
    }
}

TEST_CASE("concrete counts named in SPEC §3", "[neighbourhood]") {
    CHECK(neighbourCount(2, {NeighbourhoodType::Moore, 1}) == 8);
    CHECK(neighbourCount(3, {NeighbourhoodType::Moore, 1}) == 26);
    CHECK(neighbourCount(3, {NeighbourhoodType::VonNeumann, 1}) == 6);
    CHECK(neighbourCount(2, {NeighbourhoodType::VonNeumann, 1}) == 4);
    CHECK(neighbourCount(1, {NeighbourhoodType::Moore, 1}) == 2);
}

TEST_CASE("offsets are in canonical (dz, dy, dx) order and exclude the origin", "[neighbourhood]") {
    const auto offs = neighbourOffsets(2, {NeighbourhoodType::Moore, 1});
    REQUIRE(offs.size() == 8);
    const std::vector<Offset> expected = {
        {-1, -1, 0}, {0, -1, 0}, {1, -1, 0},
        {-1,  0, 0},             {1,  0, 0},
        {-1,  1, 0}, {0,  1, 0}, {1,  1, 0},
    };
    CHECK(offs == expected);

    const auto vn3 = neighbourOffsets(3, {NeighbourhoodType::VonNeumann, 1});
    const std::vector<Offset> expected3 = {
        {0, 0, -1}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
    };
    CHECK(vn3 == expected3);
}

TEST_CASE("offsets are unique, strictly ascending, and confined to used axes", "[neighbourhood]") {
    for (uint8_t d = 1; d <= 3; ++d) {
        for (auto type : {NeighbourhoodType::Moore, NeighbourhoodType::VonNeumann}) {
            const auto offs = neighbourOffsets(d, {type, 2});
            std::set<std::tuple<int, int, int>> seen;
            std::tuple<int, int, int> prev{-99, -99, -99};
            for (const Offset& o : offs) {
                const std::tuple<int, int, int> key{o.dz, o.dy, o.dx};
                CHECK(key > prev);
                prev = key;
                CHECK(seen.insert(key).second);
                if (d < 2) CHECK(o.dy == 0);
                if (d < 3) CHECK(o.dz == 0);
                CHECK(!(o.dx == 0 && o.dy == 0 && o.dz == 0));
            }
        }
    }
}

TEST_CASE("hexagonal neighbourhoods: counts, order, and 3D rejection", "[neighbourhood][hex]") {
    using T = NeighbourhoodType;
    CHECK(neighbourCount(2, {T::Hexagonal, 1}) == 6);
    CHECK(neighbourCount(2, {T::Hexagonal, 2}) == 18);
    CHECK(neighbourCount(2, {T::Hexagonal, 3}) == 36);
    CHECK(neighbourCount(1, {T::Hexagonal, 1}) == 2);
    for (uint8_t r = 1; r <= 4; ++r) CHECK(neighbourCount(2, {T::Hexagonal, r}) == 3u * r * (r + 1u));

    // Canonical (dz, dy, dx) order over the six axial offsets.
    const std::vector<Offset> expected = {{0, -1, 0}, {1, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {-1, 1, 0}, {0, 1, 0}};
    CHECK(neighbourOffsets(2, {T::Hexagonal, 1}) == expected);
    // Never (1,1) or (-1,-1): those are hex distance 2.
    for (const Offset& o : neighbourOffsets(2, {T::Hexagonal, 1})) CHECK(o.dx + o.dy != 2);
}
