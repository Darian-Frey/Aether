#include "core/grid.hpp"
#include "core/gpu_grid.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::core;

TEST_CASE("GridSpec footprint matches SPEC §2", "[core]") {
    GridSpec s{3, 256, 256, 256, CellType::U8};
    CHECK(s.cellCount() == 16777216);
    CHECK(s.bytesPerBuffer() == 16777216);
    CHECK(s.footprintBytes() == 33554432);          // "33.5 MB"
    s = {3, 512, 512, 512, CellType::U8};
    CHECK(s.footprintBytes() == 268435456);         // "268 MB"
    s = {2, 1024, 1024, 1, CellType::F32};
    CHECK(s.footprintBytes() == 8388608);
}

TEST_CASE("GridSpec rejects malformed extents", "[core]") {
    CHECK(GridSpec{2, 16, 16, 1}.problems().empty());
    CHECK(GridSpec{1, 16, 1, 1}.problems().empty());
    CHECK(GridSpec{3, 4, 4, 4}.problems().empty());
    CHECK_FALSE(GridSpec{0, 16, 16, 1}.problems().empty());
    CHECK_FALSE(GridSpec{4, 16, 16, 1}.problems().empty());
    CHECK_FALSE(GridSpec{2, 0, 16, 1}.problems().empty());
    CHECK_FALSE(GridSpec{1, 16, 2, 1}.problems().empty());
    CHECK_FALSE(GridSpec{2, 16, 16, 2}.problems().empty());
}

TEST_CASE("PingPong swaps and never aliases", "[core]") {
    PingPong<int> p(1, 2);
    CHECK(p.current() == 1);
    CHECK(p.next() == 2);
    CHECK(&p.current() != &p.next());
    p.swap();
    CHECK(p.current() == 2);
    CHECK(p.next() == 1);
    p.swap();
    CHECK(p.current() == 1);
}

TEST_CASE("HostGrid indexing is x fastest, then y, then z", "[core]") {
    HostGrid g({3, 4, 3, 2});
    CHECK(g.index(0, 0, 0) == 0);
    CHECK(g.index(1, 0, 0) == 1);
    CHECK(g.index(0, 1, 0) == 4);
    CHECK(g.index(0, 0, 1) == 12);
    CHECK(g.index(3, 2, 1) == 23);
    CHECK(g.current().size() == 24);
}

TEST_CASE("HostGrid edits go to current and survive a swap as next", "[core]") {
    HostGrid g({2, 4, 4, 1});
    g.set(1, 2, 0, 7);
    CHECK(g.get(1, 2) == 7);
    CHECK(g.next()[g.index(1, 2)] == 0);
    g.swap();
    CHECK(g.get(1, 2) == 0);
    CHECK(g.next()[g.index(1, 2)] == 7);
    g.clear();
    g.swap();
    CHECK(g.get(1, 2) == 0);
}

TEST_CASE("VRAM guard applies 25% headroom and passes when unknown", "[core]") {
    const GridSpec s{3, 256, 256, 256};   // 32 MiB pair, 40 MiB with headroom
    VramInfo v;
    v.source = "unknown";
    CHECK_FALSE(checkFootprint(s, v).has_value());

    v.source = "NVX";
    v.available_bytes = 40u * 1048576u;
    CHECK_FALSE(checkFootprint(s, v).has_value());
    v.available_bytes = 40u * 1048576u - 1;
    const auto e = checkFootprint(s, v);
    REQUIRE(e.has_value());
    CHECK(e->message.find("40.0 MB") != std::string::npos);
    CHECK(e->message.find("NVX") != std::string::npos);
}
