#include "ui/capture.hpp"

#include <format>

namespace aether::ui {

namespace {

// Catching up to the start of a range must not freeze the interface, so a
// frame steps at most this far. A range starting thousands of generations
// ahead therefore takes several frames to reach and stays interruptible.
constexpr uint64_t kMaxCatchUp = 256;

}  // namespace

std::string framePath(std::string_view dir, std::string_view stem, uint64_t index, int digits) {
    const std::string number = std::format("{:0{}}", index, digits < 1 ? 1 : digits);
    if (dir.empty() || dir == ".") return std::format("{}_{}.png", stem, number);
    const bool slash = dir.back() == '/';
    return std::format("{}{}{}_{}.png", dir, slash ? "" : "/", stem, number);
}

uint64_t Recording::totalFrames() const {
    if (to < from || every == 0) return 0;
    return (to - from) / every + 1;
}

bool Recording::wants(uint64_t generation) const {
    if (every == 0 || generation < from || generation > to) return false;
    return (generation - from) % every == 0;
}

uint64_t Recording::stepsBefore(uint64_t generation) const {
    // Already at a frame nobody has taken yet: draw this one as it stands.
    if (wants(generation) && generation != lastCaptured) return 0;
    if (generation < from) {
        const uint64_t gap = from - generation;
        return gap < kMaxCatchUp ? gap : kMaxCatchUp;
    }
    return every == 0 ? 1 : every;
}

bool Recording::finished(uint64_t generation) const {
    if (every == 0 || to < from) return true;
    // The last frame of the range has been taken, or the range is behind us.
    return generation > to || (generation == to && lastCaptured == to) ||
           (lastCaptured != UINT64_MAX && lastCaptured + every > to);
}

std::string Recording::pathFor(uint64_t generation) const {
    const uint64_t index = (every == 0 || generation < from) ? 0 : (generation - from) / every;
    return framePath(dir, stem, index);
}

}  // namespace aether::ui
