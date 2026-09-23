// Frame export (F-021).
//
// Two things: a single PNG of what is on screen, and a numbered sequence over
// a range of generations for something else to encode into a film. Only the
// arithmetic is here — which generations are frames, what each frame is
// called, when the run is done — because that is the part worth testing
// without a window, and the part a wrong answer in would quietly produce a
// sequence with a gap in it.
//
// A sequence is specified in *generations*, not in frames or seconds: the
// simulation's clock is the generation, and a recording that dropped or
// doubled one because a frame ran long would not be a record of the run.
// While one is being made the transport therefore stops deciding how far to
// step, and `stepsBefore` does.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aether::ui {

// `dir/stem_000042.png`. Zero-padded so the files sort in the order they were
// made, which is what every encoder expects to be handed.
std::string framePath(std::string_view dir, std::string_view stem, uint64_t index, int digits = 6);

struct Recording {
    uint64_t    from  = 0;        // first generation captured
    uint64_t    to    = 0;        // last generation that may be captured, inclusive
    uint32_t    every = 1;        // generations between captures
    std::string dir   = ".";
    std::string stem  = "frame";

    uint64_t written = 0;         // frames written so far
    uint64_t lastCaptured = UINT64_MAX;   // the generation of the last one

    // How many frames a complete run will produce.
    uint64_t totalFrames() const;

    // Is this generation one of the sequence's frames?
    bool wants(uint64_t generation) const;

    // How far to step before drawing this frame. Zero when the current
    // generation is itself a frame that has not been captured yet, so that
    // the first frame of a range starting here is not stepped past.
    uint64_t stepsBefore(uint64_t generation) const;

    // Nothing left to capture.
    bool finished(uint64_t generation) const;

    // The file the frame at this generation goes to.
    std::string pathFor(uint64_t generation) const;
};

}  // namespace aether::ui
