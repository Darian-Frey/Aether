#include "sim/scratch.hpp"

#include <algorithm>
#include <format>

namespace aether::sim {

namespace {

core::GridSpec specFor(core::GridSpec spec, const rule::RuleIR& ir) {
    spec.dimensions = ir.dimensions;
    spec.cell_type  = ir.cell_type;
    if (spec.dimensions < 3) spec.depth = 1;
    if (spec.dimensions < 2) spec.height = 1;
    return spec;
}

}  // namespace

Scratch::Scratch(core::GridSpec spec, rule::RuleIR ir, rule::CompiledRule compiled)
    : grid_(spec), ir_(std::move(ir)), compiled_(std::move(compiled)), scratch_(compiled_) {
    history_.resize(kHistory);
    for (auto& h : history_) h.resize(grid_.spec().bytesPerBuffer());
}

std::variant<Scratch, PatternError> Scratch::make(core::GridSpec spec, const rule::RuleIR& ir) {
    spec = specFor(spec, ir);
    if (const auto bad = spec.problems(); !bad.empty()) return PatternError{bad.front()};
    auto compiled = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&compiled)) return PatternError{e->message};
    return Scratch(spec, ir, std::move(std::get<rule::CompiledRule>(compiled)));
}

void Scratch::forgetHistory() {
    historyHead_ = 0;
    historyCount_ = 0;
}

std::optional<PatternError> Scratch::resize(uint32_t w, uint32_t h, uint32_t d) {
    core::GridSpec want = specFor(core::GridSpec{ir_.dimensions, w, h, d, ir_.cell_type}, ir_);
    if (const auto bad = want.problems(); !bad.empty()) return PatternError{bad.front()};
    if (want == grid_.spec()) return std::nullopt;

    // Keep whatever of the drawing still fits. The overlap is a region of the
    // old pad and a placement into the new one, so it goes through the same
    // two functions a pattern saved to disk and reopened would.
    const uint32_t ow = std::min(w, grid_.spec().width);
    const uint32_t oh = std::min(h, grid_.spec().height);
    const uint32_t od = std::min(d, grid_.spec().depth);
    std::optional<Pattern> keep;
    if (ow > 0 && oh > 0 && od > 0) {
        auto got = extractRegion(ir_, grid_.spec(), grid_.current(), 0, 0, 0, ow, oh, od);
        if (const auto* p = std::get_if<Pattern>(&got)) keep = *p;
    }

    core::HostGrid fresh(want);
    if (keep) blitPattern(*keep, want, fresh.current(), 0, 0, 0);
    grid_ = std::move(fresh);
    for (auto& hist : history_) hist.assign(want.bytesPerBuffer(), 0);
    generation_ = 0;
    forgetHistory();
    return std::nullopt;
}

std::optional<PatternError> Scratch::setRule(const rule::RuleIR& ir) {
    if (ir.dimensions != ir_.dimensions) {
        return PatternError{std::format("the pad is {}D and that rule is {}D", ir_.dimensions, ir.dimensions)};
    }
    if (ir.cell_type != ir_.cell_type) {
        return PatternError{std::format("the pad holds {} cells and that rule wants {}",
                                        core::toString(ir_.cell_type), core::toString(ir.cell_type))};
    }
    auto compiled = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&compiled)) return PatternError{e->message};

    compiled_ = std::move(std::get<rule::CompiledRule>(compiled));
    ir_ = ir;
    scratch_ = StepScratch(compiled_);
    if (ir_.cell_type == core::CellType::U8) {
        for (uint8_t& c : grid_.current()) {
            if (c >= ir_.states) c = 0;
        }
    }
    forgetHistory();
    return std::nullopt;
}

void Scratch::set(uint32_t x, uint32_t y, uint32_t z, uint8_t state) {
    const auto& s = grid_.spec();
    if (x >= s.width || y >= s.height || z >= s.depth) return;
    if (state >= ir_.states) return;
    grid_.set(x, y, z, state);
}

void Scratch::clear() {
    grid_.clear();
    generation_ = 0;
    forgetHistory();
}

void Scratch::step() {
    auto& slot = history_[historyHead_];
    const auto cells = grid_.current();
    std::copy(cells.begin(), cells.end(), slot.begin());
    historyHead_ = (historyHead_ + 1) % kHistory;
    historyCount_ = std::min(historyCount_ + 1, kHistory);

    // No cell mutation: see the header. Stepping forward and back over the
    // same generation must land on the same cells.
    cpuStep(compiled_, grid_, generation_, CellMutation{});
    ++generation_;
}

bool Scratch::stepBack() {
    if (historyCount_ == 0) return false;
    historyHead_ = (historyHead_ + kHistory - 1) % kHistory;
    --historyCount_;
    const auto& slot = history_[historyHead_];
    auto cells = grid_.current();
    std::copy(slot.begin(), slot.end(), cells.begin());
    --generation_;
    return true;
}

Pattern Scratch::toPattern() const {
    const auto& s = grid_.spec();
    auto got = toPattern(0, 0, 0, s.width, s.height, s.depth);
    return std::get<Pattern>(got);   // the whole pad always extracts
}

std::variant<Pattern, PatternError> Scratch::toPattern(uint32_t x, uint32_t y, uint32_t z,
                                                       uint32_t w, uint32_t h, uint32_t d) const {
    return extractRegion(ir_, grid_.spec(), grid_.current(), x, y, z, w, h, d);
}

std::optional<PatternError> Scratch::place(const Pattern& p, uint32_t x, uint32_t y, uint32_t z) {
    if (auto e = patternFits(p, ir_, grid_.spec(), x, y, z)) return e;
    blitPattern(p, grid_.spec(), grid_.current(), x, y, z);
    generation_ = 0;
    forgetHistory();
    return std::nullopt;
}

}  // namespace aether::sim
