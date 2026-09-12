// Headless subcommands (F-022, brought forward for the replay test).
//
//   aether headless --rule R --size WxH --seed N --seed-b N [--rule-mutation N[:M]]
//                   [--cell-mutation P] --generations G --save FILE [--cpu]
//   aether replay IN OUT [--to G] [--cpu]      replay IN from its initial state, save as OUT
//   aether compare A B                         exit 0 if the two sessions' grids match
//
// A hidden window supplies the GL context. Exit 77 when none is available,
// which ctest treats as a skip.

#pragma once

#include "ui/app.hpp"

namespace aether::ui {

constexpr int kExitNoContext = 77;

int runHeadless(const Options& opts, uint64_t generations, const std::string& savePath);
int runReplay(const std::string& in, const std::string& out, uint64_t toGeneration, bool cpu);
int runCompare(const std::string& a, const std::string& b);

}  // namespace aether::ui
