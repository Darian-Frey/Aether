// Lookup-table layout (SPEC §5).
//
// Sizes and indices for the three table-backed kinds. Everything here is
// pure arithmetic on the rule's shape; it allocates nothing. The size must be
// known before any table is built (AV-010), which is why this is a separate
// module from the backend that builds tables.
//
// The GLSL side re-implements the index functions. The two must agree; the
// equivalence tests are the check.

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aether::rule {

// SPEC §5 backend threshold. Not a tuning knob: changing it changes which
// backend runs a rule, and the two must be equivalent anyway (AV-007).
constexpr uint64_t kLutMaxEntries = 65536;

// Exact table size for a table-backed kind, or nullopt if the size does not
// fit in a uint64_t (which is certainly above the threshold). Returns nullopt
// for kinds that have no table form.
std::optional<uint64_t> tableSize(Kind kind, uint16_t states, uint32_t neighbours);

// Index arithmetic for one rule shape. Construct once per compiled rule.
class TableLayout {
public:
    TableLayout(Kind kind, uint16_t states, uint32_t neighbours);

    Kind     kind()       const { return kind_; }
    uint16_t states()     const { return states_; }
    uint32_t neighbours() const { return neighbours_; }

    // As tableSize(); nullopt if the size overflows.
    std::optional<uint64_t> size() const { return size_; }

    // Outer-totalistic. `counts` has one entry per state 1..S-1 (the count of
    // state 0 is implied). Count vectors are ranked densely in lexicographic
    // order so that the table has exactly W(N, S-1) rows per own state, where
    // W(n, m) = C(n+m, m) is the number of vectors of m non-negative integers
    // summing to at most n. For S = 2 this collapses to own*(N+1) + k.
    uint64_t indexOuterTotalistic(uint8_t own, std::span<const uint32_t> counts) const;

    // Counted-totalistic (D-016). `k` is the number of neighbours in the set
    // this own state counts.
    uint64_t indexCounted(uint8_t own, uint32_t k) const {
        return static_cast<uint64_t>(own) * (neighbours_ + 1u) + k;
    }

    // Totalistic. `sum` is own state plus every neighbour state.
    uint64_t indexTotalistic(uint32_t sum) const;

    // Non-totalistic. `neighbours` in canonical order (SPEC §3).
    uint64_t indexNonTotalistic(uint8_t own, std::span<const uint8_t> neighbours) const;

    // W(n, m) for n <= N, m <= S-1, saturating at UINT64_MAX. Exposed so the
    // GPU path can upload the same table rather than recompute it.
    uint64_t compositions(uint32_t n, uint32_t m) const;

    // Calls fn(counts) for every count vector (S-1 entries, sum <= N) in the
    // same lexicographic order the ranking uses, so the i-th call has rank i.
    // Outer-totalistic only.
    template <typename Fn>
    void forEachCountVector(Fn&& fn) const {
        std::vector<uint32_t> counts(states_ - 1u, 0);
        forEachCountVectorImpl(counts, 0, neighbours_, fn);
    }

private:
    template <typename Fn>
    void forEachCountVectorImpl(std::vector<uint32_t>& counts, size_t digit,
                                uint32_t budget, Fn& fn) const {
        if (digit == counts.size()) {
            fn(std::span<const uint32_t>(counts));
            return;
        }
        for (uint32_t v = 0; v <= budget; ++v) {
            counts[digit] = v;
            forEachCountVectorImpl(counts, digit + 1, budget - v, fn);
        }
        counts[digit] = 0;
    }

    Kind                    kind_;
    uint16_t                states_;
    uint32_t                neighbours_;
    std::optional<uint64_t> size_;
    std::vector<uint64_t>   w_;   // (N+1) x S, row-major by n
};

}  // namespace aether::rule
