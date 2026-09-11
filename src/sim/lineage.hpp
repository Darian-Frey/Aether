// Rule lineage log (F-017, SPEC §9.3).
//
// Append-only. Entry 0 is the session's initial rule; every rule change
// since is an entry with the generation at which it took effect. In memory
// each entry carries its full IR — tables are small — and the on-disk delta
// form belongs to the session format.

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aether::sim {

struct LineageEntry {
    uint64_t                   generation;
    uint64_t                   ir_hash;
    rule::RuleIR               ir;
    bool                       pinned = false;
    std::optional<std::string> name;
    std::optional<size_t>      rewound_from;   // set when this entry restores an earlier one
};

class Lineage {
public:
    size_t append(uint64_t generation, const rule::RuleIR& ir, std::optional<size_t> rewoundFrom = {});

    const std::vector<LineageEntry>& entries() const { return entries_; }
    const LineageEntry& at(size_t i) const { return entries_.at(i); }
    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    const LineageEntry& back() const { return entries_.back(); }

    void pin(size_t i, std::string name);
    void unpin(size_t i);

private:
    std::vector<LineageEntry> entries_;
};

}  // namespace aether::sim
