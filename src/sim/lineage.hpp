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

enum class LineageOrigin : uint8_t { Initial, User, Mutation, Rewind };

struct LineageEntry {
    uint64_t                   generation;
    uint64_t                   ir_hash;
    rule::RuleIR               ir;
    bool                       pinned = false;
    std::optional<std::string> name;
    LineageOrigin              origin = LineageOrigin::User;
    std::optional<size_t>      rewound_from;   // set when origin == Rewind
    size_t                     journal_index = 0;   // journal length when this entry was made;
                                                    // replaying events [0, journal_index) and, for a
                                                    // Mutation, the mutation at `generation`, reaches it
};

class Lineage {
public:
    size_t append(uint64_t generation, const rule::RuleIR& ir, LineageOrigin origin, size_t journalIndex,
                  std::optional<size_t> rewoundFrom = {});
    void truncate(size_t keep) { if (keep < entries_.size()) entries_.resize(keep); }

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
