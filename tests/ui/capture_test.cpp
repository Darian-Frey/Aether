// Frame export arithmetic (F-021).
//
// No window here: which generations are frames and what each is called is
// exactly the part that should be checkable without one, and a wrong answer
// would quietly produce a sequence with a gap or a repeat in it.

#include "ui/capture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace aether;
using ui::Recording;

TEST_CASE("a frame path is zero-padded so the files sort in order", "[capture]") {
    CHECK(ui::framePath("out", "frame", 0) == "out/frame_000000.png");
    CHECK(ui::framePath("out", "frame", 42) == "out/frame_000042.png");
    CHECK(ui::framePath("out/", "frame", 7) == "out/frame_000007.png");
    CHECK(ui::framePath("", "life", 3) == "life_000003.png");
    CHECK(ui::framePath(".", "life", 3) == "life_000003.png");
    // Past the padding the number simply gets longer rather than wrapping.
    CHECK(ui::framePath("out", "frame", 1234567) == "out/frame_1234567.png");
}

TEST_CASE("a recording captures exactly the generations in its range", "[capture]") {
    Recording r;
    r.from = 10; r.to = 20; r.every = 5;

    CHECK(r.totalFrames() == 3);        // 10, 15, 20
    CHECK(r.wants(10));
    CHECK(r.wants(15));
    CHECK(r.wants(20));
    CHECK_FALSE(r.wants(9));
    CHECK_FALSE(r.wants(11));
    CHECK_FALSE(r.wants(21));
    CHECK_FALSE(r.wants(25));
}

TEST_CASE("every generation is a frame when the step is one", "[capture]") {
    Recording r;
    r.from = 0; r.to = 4; r.every = 1;
    CHECK(r.totalFrames() == 5);
    for (uint64_t g = 0; g <= 4; ++g) CHECK(r.wants(g));
    CHECK_FALSE(r.wants(5));
}

TEST_CASE("a range that does not divide evenly stops at the last frame inside it", "[capture]") {
    Recording r;
    r.from = 0; r.to = 10; r.every = 4;
    CHECK(r.totalFrames() == 3);        // 0, 4, 8 — 12 is outside
    CHECK(r.wants(8));
    CHECK_FALSE(r.wants(10));
    CHECK_FALSE(r.wants(12));
}

TEST_CASE("the first frame of a range starting here is not stepped past", "[capture]") {
    Recording r;
    r.from = 100; r.to = 110; r.every = 5;
    // Sitting on the first frame, uncaptured: step nowhere, draw it as it is.
    CHECK(r.stepsBefore(100) == 0);
    // Having captured it, the next frame is `every` away.
    r.lastCaptured = 100;
    CHECK(r.stepsBefore(100) == 5);
}

TEST_CASE("a range ahead of the run is caught up to, a bounded amount per frame", "[capture]") {
    Recording r;
    r.from = 50; r.to = 60; r.every = 1;
    CHECK(r.stepsBefore(0) == 50);
    CHECK(r.stepsBefore(49) == 1);
    CHECK(r.stepsBefore(50) == 0);

    // A range thousands of generations away must not freeze the interface,
    // so catching up is spread over frames.
    Recording far;
    far.from = 1'000'000; far.to = 1'000'010; far.every = 1;
    const uint64_t step = far.stepsBefore(0);
    CHECK(step > 0);
    CHECK(step <= 1024);
}

TEST_CASE("a recording walks its range and then reports itself finished", "[capture]") {
    Recording r;
    r.from = 3; r.to = 9; r.every = 3;
    r.dir = "shots";

    // Drive it exactly as the frame loop does: step, then capture if wanted.
    std::vector<std::pair<uint64_t, std::string>> taken;
    uint64_t generation = 0;
    for (int guard = 0; guard < 100 && !r.finished(generation); ++guard) {
        generation += r.stepsBefore(generation);
        if (r.wants(generation) && generation != r.lastCaptured) {
            taken.push_back({generation, r.pathFor(generation)});
            r.lastCaptured = generation;
            ++r.written;
        }
    }
    REQUIRE(taken.size() == r.totalFrames());
    CHECK(taken[0] == std::pair<uint64_t, std::string>{3, "shots/frame_000000.png"});
    CHECK(taken[1] == std::pair<uint64_t, std::string>{6, "shots/frame_000001.png"});
    CHECK(taken[2] == std::pair<uint64_t, std::string>{9, "shots/frame_000002.png"});
    CHECK(r.finished(generation));
}

TEST_CASE("a one-frame range is a single capture", "[capture]") {
    Recording r;
    r.from = 7; r.to = 7; r.every = 1;
    CHECK(r.totalFrames() == 1);
    CHECK(r.stepsBefore(7) == 0);
    r.lastCaptured = 7;
    CHECK(r.finished(7));
}

TEST_CASE("a degenerate range asks for nothing rather than looping", "[capture]") {
    Recording backwards;
    backwards.from = 10; backwards.to = 5;
    CHECK(backwards.totalFrames() == 0);
    CHECK(backwards.finished(0));

    Recording zeroStep;
    zeroStep.from = 0; zeroStep.to = 10; zeroStep.every = 0;
    CHECK(zeroStep.totalFrames() == 0);
    CHECK(zeroStep.finished(0));
    CHECK_FALSE(zeroStep.wants(0));
}
