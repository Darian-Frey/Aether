// Headless subcommands (F-022, brought forward for the replay test).
//
//   aether headless --rule R --size WxH --seed N --seed-b N [--rule-mutation N[:M]]
//                   [--cell-mutation P] --generations G [--save FILE] [--cpu]
//                   [--png FILE] [--frame-dir DIR] [--frame-every N] [--frame-scale N]
//   aether replay IN OUT [--to G] [--cpu]      replay IN from its initial state, save as OUT
//   aether compare A B                         exit 0 if the two sessions' grids match
//
// A hidden window supplies the GL context. Exit 77 when none is available,
// which ctest treats as a skip.
//
// Images are rendered through the same `Renderer2D` and the same palette pass
// the window uses, into an offscreen texture at one pixel per cell (or
// `--frame-scale` of them). Mapping states to colours on the host instead
// would need no context at all, and would be a second implementation of
// `shaders/palette2d.frag` free to drift from it on ageing tails and on the
// continuous ramp — which is the mistake AV-017 is about, in a place where
// nobody would be comparing the two outputs. A hidden window is the smaller
// cost, and these subcommands already require one.
//
// The generation arithmetic behind a sequence is `ui::Recording`, the same
// type the window's Export section drives (F-021). Two drivers, one set of
// rules about which generation is which frame.

#pragma once

#include "ui/app.hpp"

namespace aether::ui {

constexpr int kExitNoContext = 77;

// What a headless run should write, beyond the session itself.
struct DumpOptions {
    std::string png;          // the final grid, as one PNG
    std::string framesDir;    // a numbered sequence, one per `frameEvery` generations
    uint32_t    frameEvery = 1;
    uint32_t    frameScale = 1;   // pixels per cell
};

int runHeadless(const Options& opts, uint64_t generations, const std::string& savePath,
                const DumpOptions& dump = {});
int runReplay(const std::string& in, const std::string& out, uint64_t toGeneration, bool cpu);
int runCompare(const std::string& a, const std::string& b);

}  // namespace aether::ui
