#include "sim/lineage.hpp"

namespace aether::sim {

size_t Lineage::append(uint64_t generation, const rule::RuleIR& ir, std::optional<size_t> rewoundFrom) {
    LineageEntry e;
    e.generation = generation;
    e.ir_hash = rule::irHash(ir);
    e.ir = ir;
    e.rewound_from = rewoundFrom;
    entries_.push_back(std::move(e));
    return entries_.size() - 1;
}

void Lineage::pin(size_t i, std::string name) {
    LineageEntry& e = entries_.at(i);
    e.pinned = true;
    e.name = std::move(name);
    e.ir.metadata.name = e.name;
}

void Lineage::unpin(size_t i) {
    LineageEntry& e = entries_.at(i);
    e.pinned = false;
}

}  // namespace aether::sim
