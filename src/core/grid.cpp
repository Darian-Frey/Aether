#include "core/grid.hpp"

#include <algorithm>
#include <format>

namespace aether::core {

std::vector<std::string> GridSpec::problems() const {
    std::vector<std::string> out;
    if (dimensions < 1 || dimensions > 3) {
        out.push_back(std::format("dimensions must be 1, 2 or 3 (got {})", dimensions));
        return out;
    }
    if (width == 0 || height == 0 || depth == 0) {
        out.push_back("every extent must be at least 1");
    }
    if (dimensions < 2 && height != 1) {
        out.push_back(std::format("height must be 1 for a {}D grid (got {})", dimensions, height));
    }
    if (dimensions < 3 && depth != 1) {
        out.push_back(std::format("depth must be 1 for a {}D grid (got {})", dimensions, depth));
    }
    return out;
}

HostGrid::HostGrid(GridSpec spec)
    : spec_(spec),
      pair_(std::vector<uint8_t>(spec.bytesPerBuffer(), 0),
            std::vector<uint8_t>(spec.bytesPerBuffer(), 0)) {}

void HostGrid::clear() {
    std::ranges::fill(pair_.current(), uint8_t{0});
    std::ranges::fill(pair_.next(), uint8_t{0});
}

}  // namespace aether::core
