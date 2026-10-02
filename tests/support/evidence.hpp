// Whether a comparison was of anything (IMP-011).
//
// A sweep that steps a rule for a thousand generations and compares two grids
// bitwise passes when both grids are empty. It passes *fast*, which is worse,
// because nothing looks wrong: the case is green, the suite is green, and the
// comparison carried no evidence at all.
//
// This is not hypothetical. AV-015's own history records the mistake in this
// suite: a 128² comparison run to 10,000 generations "did compare identical —
// but the rule used there dies out, so the comparison was of two empty grids and
// carried no evidence at all". That claim was withdrawn in 2026-09-17 and the
// guard it implies was never added, so every comparison written since has been
// able to fail the same way.
//
// Two shapes of worthlessness, and both are caught here:
//
//   * **extinction** — the final grid is all zeroes, so there is nothing left
//     that could have differed between the two paths;
//   * **a frozen grid** — the final grid is bit-for-bit the seed, so the rule
//     never moved a cell and the step was never really exercised.
//
// The test works on raw bytes rather than on cells, which is what makes one
// implementation serve every cell type: "all zero" and "unchanged" are byte-level
// questions, and all-zero bytes is exactly an empty `u8` grid, an empty `u32`
// field and a `f32` field of 0.0 alike.

#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace aether::test {

inline std::optional<std::string> comparisonEvidence(std::span<const uint8_t> seed,
                                                     std::span<const uint8_t> settled) {
    size_t nonZero = 0;
    for (uint8_t b : settled) {
        if (b != 0) ++nonZero;
    }
    if (nonZero == 0) {
        return std::string("the run ended empty, so the comparison was of two empty grids "
                           "and carried no evidence (IMP-011)");
    }
    if (seed.size() == settled.size()) {
        bool moved = false;
        for (size_t i = 0; i < seed.size() && !moved; ++i) {
            moved = seed[i] != settled[i];
        }
        if (!moved) {
            return std::string("the run ended bit-for-bit as it was seeded, so nothing was "
                               "stepped and the comparison carried no evidence (IMP-011)");
        }
    }
    return std::nullopt;
}

// The same, as a fraction of cells that are non-zero, for a caller that wants to
// say more than "not empty" — a rule expected to hold a population, say.
inline double nonZeroFraction(std::span<const uint8_t> cells, uint32_t bytesPerCell) {
    if (cells.empty() || bytesPerCell == 0) return 0.0;
    size_t live = 0;
    const size_t n = cells.size() / bytesPerCell;
    for (size_t i = 0; i < n; ++i) {
        for (uint32_t b = 0; b < bytesPerCell; ++b) {
            if (cells[i * bytesPerCell + b] != 0) { ++live; break; }
        }
    }
    return static_cast<double>(live) / static_cast<double>(n);
}

}  // namespace aether::test
