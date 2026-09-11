#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

using namespace aether::rule;

TEST_CASE("table sizes match the SPEC §5 worked examples", "[table]") {
    CHECK(tableSize(Kind::OuterTotalistic, 2, 8)  == 18);
    CHECK(tableSize(Kind::OuterTotalistic, 4, 8)  == 660);
    CHECK(tableSize(Kind::NonTotalistic,   2, 8)  == 512);
    CHECK(tableSize(Kind::OuterTotalistic, 2, 26) == 54);
    CHECK(tableSize(Kind::NonTotalistic,   2, 26) == 2ull * (1ull << 26));
    CHECK(tableSize(Kind::Totalistic,      2, 8)  == 10);
    CHECK(tableSize(Kind::Totalistic,      3, 8)  == 19);
    CHECK_FALSE(tableSize(Kind::Expression, 2, 8).has_value());
    CHECK_FALSE(tableSize(Kind::Continuous, 2, 8).has_value());
}

TEST_CASE("backend threshold is placed where SPEC §5 says", "[table]") {
    CHECK(kLutMaxEntries == 65536);
    CHECK(*tableSize(Kind::NonTotalistic, 2, 8)  <= kLutMaxEntries);
    CHECK(*tableSize(Kind::NonTotalistic, 2, 26) >  kLutMaxEntries);
    // 2 * 2^15 = 65536 sits exactly on the threshold and stays on the table
    // side; one more neighbour tips it over.
    CHECK(*tableSize(Kind::NonTotalistic, 2, 15) == kLutMaxEntries);
    CHECK(*tableSize(Kind::NonTotalistic, 2, 16) >  kLutMaxEntries);
}

TEST_CASE("sizes that overflow 64 bits report as absent rather than wrapping", "[table]") {
    CHECK_FALSE(tableSize(Kind::NonTotalistic, 256, 26).has_value());
    CHECK_FALSE(tableSize(Kind::NonTotalistic, 3, 64).has_value());     // 3^65
    CHECK_FALSE(tableSize(Kind::OuterTotalistic, 256, 124).has_value());
    CHECK(tableSize(Kind::NonTotalistic, 2, 62).has_value());           // 2 * 2^62 = 2^63
    CHECK_FALSE(tableSize(Kind::NonTotalistic, 2, 63).has_value());     // 2 * 2^63 = 2^64
}

TEST_CASE("binary outer-totalistic index is own*(N+1)+k", "[table]") {
    const TableLayout L(Kind::OuterTotalistic, 2, 8);
    REQUIRE(L.size() == 18);
    for (uint8_t own = 0; own < 2; ++own) {
        for (uint32_t k = 0; k <= 8; ++k) {
            const uint32_t counts[1] = {k};
            CHECK(L.indexOuterTotalistic(own, counts) == own * 9u + k);
        }
    }
}

TEST_CASE("multi-state outer-totalistic ranking is a dense bijection", "[table]") {
    // Enumerate every count vector for S=4, N=8 and check the indices form
    // exactly 0..size-1 with no gaps and no collisions, per own state.
    const TableLayout L(Kind::OuterTotalistic, 4, 8);
    REQUIRE(L.size() == 660);
    const uint64_t perState = 660 / 4;
    for (uint8_t own = 0; own < 4; ++own) {
        std::set<uint64_t> seen;
        for (uint32_t c1 = 0; c1 <= 8; ++c1) {
            for (uint32_t c2 = 0; c1 + c2 <= 8; ++c2) {
                for (uint32_t c3 = 0; c1 + c2 + c3 <= 8; ++c3) {
                    const uint32_t counts[3] = {c1, c2, c3};
                    const uint64_t idx = L.indexOuterTotalistic(own, counts);
                    CHECK(idx >= own * perState);
                    CHECK(idx <  (own + 1u) * perState);
                    CHECK(seen.insert(idx).second);
                }
            }
        }
        CHECK(seen.size() == perState);
    }
}

TEST_CASE("ranking is lexicographic ascending in the count vector", "[table]") {
    const TableLayout L(Kind::OuterTotalistic, 3, 8);
    const uint32_t a[2] = {0, 0};
    const uint32_t b[2] = {0, 1};
    const uint32_t c[2] = {1, 0};
    CHECK(L.indexOuterTotalistic(0, a) == 0);
    CHECK(L.indexOuterTotalistic(0, b) == 1);
    CHECK(L.indexOuterTotalistic(0, b) < L.indexOuterTotalistic(0, c));
}

TEST_CASE("non-totalistic signature is little-endian base S in canonical order", "[table]") {
    const TableLayout L(Kind::NonTotalistic, 2, 8);
    REQUIRE(L.size() == 512);
    const uint8_t n0[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    const uint8_t n7[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    CHECK(L.indexNonTotalistic(0, n0) == 1);
    CHECK(L.indexNonTotalistic(0, n7) == 128);
    CHECK(L.indexNonTotalistic(1, n0) == 256 + 1);

    const TableLayout L3(Kind::NonTotalistic, 3, 4);
    REQUIRE(L3.size() == 3 * 81);
    const uint8_t m[4] = {2, 1, 0, 2};   // 2 + 1*3 + 0*9 + 2*27 = 59
    CHECK(L3.indexNonTotalistic(0, m) == 59);
    CHECK(L3.indexNonTotalistic(2, m) == 2 * 81 + 59);
}

TEST_CASE("totalistic index is the plain sum", "[table]") {
    const TableLayout L(Kind::Totalistic, 3, 8);
    REQUIRE(L.size() == 19);
    CHECK(L.indexTotalistic(0)  == 0);
    CHECK(L.indexTotalistic(18) == 18);
}

TEST_CASE("forEachCountVector visits vectors in rank order", "[table]") {
    const TableLayout L(Kind::OuterTotalistic, 4, 8);
    uint64_t expected = 0;
    L.forEachCountVector([&](std::span<const uint32_t> counts) {
        REQUIRE(counts.size() == 3);
        CHECK(counts[0] + counts[1] + counts[2] <= 8);
        CHECK(L.indexOuterTotalistic(0, counts) == expected);
        ++expected;
    });
    CHECK(expected == 165);

    const TableLayout B(Kind::OuterTotalistic, 2, 8);
    uint32_t seen = 0;
    B.forEachCountVector([&](std::span<const uint32_t> counts) {
        CHECK(counts[0] == seen);
        ++seen;
    });
    CHECK(seen == 9);
}
