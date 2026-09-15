#include "ui/app.hpp"

#include "core/gl.hpp"
#include "rule/compile.hpp"
#include "sim/session.hpp"

#include <imgui.h>
#include <raylib.h>
#include <rlgl.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>

namespace aether::ui {

namespace {

constexpr float kPanelWidth = 360.0f;

}  // namespace

int App::run() {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(opts_.windowWidth, opts_.windowHeight, "Aether");
    if (!IsWindowReady()) return 1;
    int exitCode = 0;
    {
        rlImGuiSetup(true);

        auto made = render::Renderer2D::create();
        if (const auto* e = std::get_if<core::Error>(&made)) {
            log_.error(e->message);
            exitCode = 1;
        } else {
            renderer_.emplace(std::get<render::Renderer2D>(std::move(made)));
        }
        auto made3 = render::Renderer3D::create();
        if (const auto* e = std::get_if<core::Error>(&made3)) {
            log_.error(e->message);
            exitCode = 1;
        } else {
            renderer3d_.emplace(std::get<render::Renderer3D>(std::move(made3)));
        }

        viewport_ = render::Rect{kPanelWidth, 0.0f,
                                 static_cast<float>(GetScreenWidth()) - kPanelWidth,
                                 static_cast<float>(GetScreenHeight())};
        std::strncpy(ruleText_.data(), opts_.rule.c_str(), ruleText_.size() - 1);
        newWidth_ = static_cast<int>(opts_.width);
        newHeight_ = static_cast<int>(opts_.height);
        newDepth_ = static_cast<int>(opts_.depth);
        targetGpsLog_ = static_cast<float>(std::log10(std::max(0.1, opts_.targetGps)));
        if (opts_.ruleMutationInterval > 0) {
            ruleMutationOn_ = true;
            ruleInterval_ = static_cast<int>(opts_.ruleMutationInterval);
            ruleMagnitude_ = static_cast<int>(std::max(1u, opts_.ruleMutationMagnitude));
        }
        if (opts_.cellMutationP > 0.0) {
            cellMutationOn_ = true;
            cellMutationLog_ = static_cast<float>(std::log10(opts_.cellMutationP));
        }
        cellMutationBlock_ = static_cast<int>(opts_.cellMutationBlock);

        std::strncpy(sessionPath_.data(), "session.aether", sessionPath_.size() - 1);
        // Where a rule library might be: an override, the working directory,
        // then beside and above the binary, so a build tree and an install
        // both work without configuration.
        const std::string exeDir = GetApplicationDirectory();
        const char* env = std::getenv("AETHER_RULES");
        library_ = rule::loadLibrary({env ? env : "", "rules", exeDir + "rules", exeDir + "../rules"});
        if (!library_.empty()) log_.info(std::format("{} rules in the library", library_.size()));
        if (exitCode == 0 && !opts_.load.empty()) {
            loadSessionFrom(opts_.load);
            if (!sim_) exitCode = 1;
        } else if (exitCode == 0) {
            ctx_.dimensions = opts_.depth > 1 ? 3 : 2;
            ruleLanguage_ = opts_.ruleIsLua ? 1 : 0;
            if (!opts_.ruleIsLua && opts_.rule.starts_with("@")) {
                const std::string wanted = opts_.rule.substr(1);
                const auto it = std::find_if(library_.begin(), library_.end(),
                                             [&](const rule::LibraryRule& r) { return r.id == wanted; });
                if (it == library_.end()) {
                    log_.error(std::format("no rule '{}' in the library", wanted));
                } else {
                    std::strncpy(ruleText_.data(), it->source.c_str(), ruleText_.size() - 1);
                    ruleLanguage_ = it->isLua ? 1 : 0;
                    ctx_.dimensions = it->dimensions;
                    paletteOverrides_ = it->palette;
                    if (it->dimensions == 3 && opts_.depth == 1) opts_.depth = opts_.width = opts_.height = 64;
                }
            }
            auto initial = compileRuleSource();
            if (!initial) {
                log_.error("initial rule: " + ruleError_);
                ruleLanguage_ = 0;
                std::strncpy(ruleText_.data(), opts_.depth > 1 ? "B5/S45" : "B3/S23", ruleText_.size() - 1);
                initial = compileRuleSource();
            }
            if (!createSimulation(opts_.width, opts_.height, opts_.depth, *initial, opts_.cpu ? sim::Path::Cpu : sim::Path::Gpu)) {
                exitCode = 1;
            } else {
                sim_->scheduler().setTargetRate(opts_.targetGps);
                sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
                sim_->scheduler().setPaused(false);
            }
        }

        int frames = 0;
        while (exitCode == 0 && !WindowShouldClose()) {
            const double dt = GetFrameTime();
            viewport_ = render::Rect{kPanelWidth, 0.0f,
                                     static_cast<float>(GetScreenWidth()) - kPanelWidth,
                                     static_cast<float>(GetScreenHeight())};
            if (IsWindowResized()) fitView();

            updateCanvas(dt);
            if (sim_) {
                // Some drivers (Mesa iris) defer the vsync throttle to the
                // first GL call after the swap. Take that wait here, so the
                // scheduler's wall-clock budget measures stepping and not
                // the previous frame's presentation.
                glFinish();
                sim_->frame(dt);
                if (sim_->lineage().size() != lastLineageSize_) {
                    lastLineageSize_ = sim_->lineage().size();
                    const auto& e = sim_->lineage().back();
                    log_.info(std::format("gen {}: rule -> {:#018x}{}", e.generation, e.ir_hash,
                                          e.rewound_from ? " (rewind)" : ""));
                    refreshRuleSummary();
                }
            }

            BeginDrawing();
            ClearBackground(Color{18, 18, 22, 255});
            if (sim_ && is3D() && renderer3d_) {
                renderer3d_->draw(sim_->texture(), sim_->spec(), orbit_, volumeSettings(), viewport_,
                                  GetRenderWidth(), GetRenderHeight());
            } else if (sim_ && renderer_) {
                renderer_->draw(sim_->texture(), sim_->spec(), view_, viewport_,
                                GetRenderWidth(), GetRenderHeight(), sim_->rule().states);
            }
            rlImGuiBegin();
            drawPanels();
            rlImGuiEnd();

            // Capture before the swap: the back buffer's contents after a
            // swap are undefined, and on Mesa they are often black.
            const bool lastFrame = opts_.exitAfterFrames > 0 && ++frames >= opts_.exitAfterFrames;
            if (lastFrame && !opts_.screenshot.empty()) {
                rlDrawRenderBatchActive();
                TakeScreenshot(opts_.screenshot.c_str());
            }
            EndDrawing();
            if (lastFrame) break;
        }

        rlImGuiShutdown();
        sim_.reset();
        renderer_.reset();
        renderer3d_.reset();
    }
    CloseWindow();
    return exitCode;
}

// One description of the running rule, used wherever it is shown.
void App::refreshRuleSummary() {
    if (!sim_) return;
    const auto& ir = sim_->rule();
    const auto& compiled = sim_->compiled();
    ruleSummary_ = std::format("{} · {} states{} · N={} · {} · {} · {:#018x}",
                               ir.metadata.name.value_or(std::string(rule::toString(ir.kind))), ir.states,
                               ir.metadata.decay_from
                                   ? std::format(" ({} live, decay {})", *ir.metadata.decay_from,
                                                 ir.states - *ir.metadata.decay_from)
                                   : std::string{},
                               compiled.neighbourCount(), rule::toString(ir.boundary),
                               compiled.backend == rule::Backend::Codegen
                                   ? std::string("codegen")
                                   : std::format("table {}", compiled.table.size()),
                               compiled.ir_hash);
}

bool App::is3D() const { return sim_ && sim_->spec().dimensions == 3; }

render::VolumeSettings App::volumeSettings() const {
    render::VolumeSettings v;
    if (!sim_) return v;
    const double ext[3] = {double(sim_->spec().width), double(sim_->spec().height), double(sim_->spec().depth)};
    for (size_t a = 0; a < 3; ++a) {
        v.clipMin[a] = clipLo_[a] * ext[a];
        v.clipMax[a] = std::max(clipHi_[a] * ext[a], v.clipMin[a] + 1.0);
    }
    if (sliceMode_) {
        const size_t ax = static_cast<size_t>(sliceAxis_);
        v.clipMin[ax] = sliceIndex_;
        v.clipMax[ax] = sliceIndex_ + 1.0;
    }
    v.opacity = opacity_;
    return v;
}

bool App::createSimulation(uint32_t width, uint32_t height, uint32_t depth, const rule::RuleIR& ir, sim::Path path) {
    core::GridSpec spec{static_cast<uint8_t>(depth > 1 ? 3 : 2), width, height, depth, core::CellType::U8};
    auto made = sim::Simulation::create(spec, ir, path, opts_.seed, opts_.seedB);
    if (const auto* e = std::get_if<core::Error>(&made)) {
        log_.error(std::format("grid {}x{}: {}", width, height, e->message));
        return false;
    }
    sim_.emplace(std::get<sim::Simulation>(std::move(made)));
    sim_->scheduler().setTargetRate(std::pow(10.0, targetGpsLog_));
    sim_->setCellMutation(cellMutationOn_ ? std::pow(10.0, cellMutationLog_) : 0.0,
                          static_cast<uint8_t>(cellMutationBlock_));
    sim_->setRuleMutation({ruleMutationOn_, static_cast<uint32_t>(ruleInterval_), static_cast<uint32_t>(ruleMagnitude_)});
    lastLineageSize_ = sim_->lineage().size();
    refreshRuleSummary();
    log_.info(std::format("grid {}x{}x{} on {} path; rule {}", width, height, depth,
                          path == sim::Path::Gpu ? "GPU" : "CPU", ir.metadata.source_notation.value_or("?")));
    sliceIndex_ = static_cast<int>(depth / 2);
    applyPaletteForStates();
    fitView();
    return true;
}

bool App::adoptSimulation(sim::Simulation&& s, const char* what) {
    sim_.emplace(std::move(s));
    sim_->scheduler().setTargetRate(std::pow(10.0, targetGpsLog_));
    sim_->scheduler().setPaused(true);
    newWidth_ = static_cast<int>(sim_->spec().width);
    newHeight_ = static_cast<int>(sim_->spec().height);
    newDepth_ = static_cast<int>(sim_->spec().depth);
    sliceIndex_ = static_cast<int>(sim_->spec().depth / 2);
    ctx_.boundary = sim_->rule().boundary;
    ctx_.dimensions = sim_->spec().dimensions;
    const auto& ir = sim_->rule();
    std::strncpy(ruleText_.data(), ir.metadata.source_notation.value_or(ir.metadata.name.value_or("")).c_str(), ruleText_.size() - 1);
    ruleMutationOn_ = sim_->ruleMutation().enabled;
    ruleInterval_ = static_cast<int>(sim_->ruleMutation().interval);
    ruleMagnitude_ = static_cast<int>(sim_->ruleMutation().magnitude);
    cellMutationOn_ = sim_->cellMutation() > 0.0;
    cellMutationBlock_ = sim_->cellMutationBlock();
    if (cellMutationOn_) cellMutationLog_ = static_cast<float>(std::log10(sim_->cellMutation()));
    lastLineageSize_ = sim_->lineage().size();
    refreshRuleSummary();
    ruleError_.clear();
    applyPaletteOverrides(ir);
    density_.assign(ir.states - 1u, 0.1f);
    density_[0] = ir.states == 2 ? 0.3f : 0.2f;
    fitView();
    log_.info(std::format("{}: generation {}, {} lineage entries, {} journal events (paused)", what,
                          sim_->generation(), sim_->lineage().size(), sim_->journal().size()));
    return true;
}

void App::saveSessionTo(const std::string& path) {
    if (!sim_) return;
    if (auto e = sim::saveSession(path, sim_->session())) log_.error("save: " + e->message);
    else log_.info(std::format("saved {} at generation {}", path, sim_->generation()));
}

void App::loadSessionFrom(const std::string& path) {
    auto loaded = sim::loadSession(path);
    if (const auto* e = std::get_if<sim::SessionError>(&loaded)) { log_.error("load: " + e->message); return; }
    const sim::Path path_ = sim_ ? sim_->path() : (opts_.cpu ? sim::Path::Cpu : sim::Path::Gpu);
    sim_.reset();
    auto made = sim::Simulation::resume(std::get<sim::Session>(loaded), path_);
    if (const auto* e = std::get_if<core::Error>(&made)) { log_.error("load: " + e->message); return; }
    adoptSimulation(std::get<sim::Simulation>(std::move(made)), ("loaded " + path).c_str());
}

void App::verifyReplay() {
    if (!sim_) return;
    const sim::Session snap = sim_->session();
    auto made = sim::Simulation::replay(snap, {snap.generation}, sim_->path() == sim::Path::Gpu ? sim::Path::Cpu : sim::Path::Gpu);
    if (const auto* e = std::get_if<core::Error>(&made)) { log_.error("replay: " + e->message); return; }
    auto replayed = std::get<sim::Simulation>(std::move(made));
    replayed.syncToHost();
    size_t diff = 0;
    const auto cells = replayed.host().current();
    for (size_t i = 0; i < cells.size(); ++i) diff += cells[i] != snap.current[i];
    bool lineageOk = replayed.lineage().size() == snap.lineage.size();
    for (size_t i = 0; lineageOk && i < snap.lineage.size(); ++i) lineageOk = replayed.lineage().at(i).ir_hash == snap.lineage[i].ir_hash;
    if (diff == 0 && lineageOk) {
        log_.info(std::format("replay verified: {} generations on the other path reproduce the grid and {} rules",
                              snap.generation, snap.lineage.size()));
    } else {
        log_.error(std::format("replay DIVERGED: {} cells differ; lineage {}", diff, lineageOk ? "matches" : "differs"));
    }
}

void App::applyPaletteForStates() {
    if (!sim_ || !renderer_) return;
    const uint16_t states = sim_->rule().states;
    applyPaletteOverrides(sim_->rule());
    density_.assign(states - 1u, 0.0f);
    density_[0] = states == 2 ? 0.3f : 0.2f;
    for (size_t i = 1; i < density_.size(); ++i) density_[i] = 0.1f;
}

// A rule's own palette, laid over the default for its state count (SPEC §13).
void App::applyPaletteOverrides(const rule::RuleIR& ir) {
    if (!renderer_) return;
    render::Palette pal = render::Palette::defaultFor(ir.states, ir.metadata.decay_from);
    for (const rule::PaletteOverride& o : paletteOverrides_) {
        if (o.state < 256) pal.entries[o.state] = {o.rgba[0], o.rgba[1], o.rgba[2], o.rgba[3]};
    }
    renderer_->setPalette(pal);
    renderer_->setDecayFrom(ir.metadata.decay_from);
    if (renderer3d_) renderer3d_->setPalette(pal);
}

// Puts a library rule in the editor and compiles it, rebuilding the grid
// when the rule wants a different number of dimensions.
void App::loadLibraryRule(const rule::LibraryRule& entry) {
    std::strncpy(ruleText_.data(), entry.source.c_str(), ruleText_.size() - 1);
    ruleText_[ruleText_.size() - 1] = '\0';
    ruleLanguage_ = entry.isLua ? 1 : 0;
    ctx_.dimensions = entry.dimensions;
    paletteOverrides_ = entry.palette;

    if (sim_ && sim_->spec().dimensions != entry.dimensions) {
        auto compiled = compileRuleSource();
        if (!compiled) { log_.error("rule: " + ruleError_); return; }
        const uint32_t side = entry.dimensions == 3 ? 64u : 512u;
        const sim::Path path = sim_->path();
        sim_.reset();
        if (!createSimulation(side, side, entry.dimensions == 3 ? side : 1u, *compiled, path)) return;
        sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
        log_.info(std::format("loaded {} and rebuilt the grid at {}D", entry.name, entry.dimensions));
    } else if (compileRuleText()) {
        log_.info(std::format("loaded {}", entry.name));
    }
    if (sim_) applyPaletteOverrides(sim_->rule());
}

// The DSL or Lua, whichever the panel names. Both end at a validated IR.
std::optional<rule::RuleIR> App::compileRuleSource() {
    if (ruleLanguage_ == 1) {
        rule::LuaContext lctx;
        lctx.dimensions = ctx_.dimensions;
        lctx.boundary = ctx_.boundary;
        auto r = rule::compileLua(ruleText_.data(), lctx);
        if (const auto* e = std::get_if<rule::LuaError>(&r)) {
            ruleError_ = e->message;
            return std::nullopt;
        }
        return std::get<rule::RuleIR>(std::move(r));
    }
    auto parsed = rule::parseDsl(ruleText_.data(), ctx_);
    if (!parsed) {
        ruleError_ = std::format("{}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message);
        return std::nullopt;
    }
    return *parsed.ir;
}

bool App::compileRuleText() {
    auto compiled = compileRuleSource();
    if (!compiled) {
        log_.error("rule: " + ruleError_);
        return false;
    }
    if (!sim_) return false;
    const uint16_t oldStates = sim_->rule().states;
    if (auto e = sim_->setRule(*compiled)) {
        ruleError_ = e->message;
        log_.error("rule: " + ruleError_);
        return false;
    }
    ruleError_.clear();
    const auto& ir = sim_->rule();
    refreshRuleSummary();
    log_.info(std::format("compiled {} -> {}",
                          ir.metadata.source_notation.value_or(ir.metadata.name.value_or("rule")).substr(0, 40),
                          sim_->backend() == rule::Backend::Lut
                              ? std::format("table backend, {} entries", sim_->compiled().table.size())
                              : std::format("codegen backend, {} lines of GLSL",
                                            std::count(sim_->compiled().glsl.begin(),
                                                       sim_->compiled().glsl.end(), '\n'))));
    if (ir.states != oldStates) applyPaletteForStates();
    if (brush_.state >= ir.states) brush_.state = static_cast<uint8_t>(ir.states - 1);
    const auto lattice = ir.neighbourhood.type == rule::NeighbourhoodType::Hexagonal ? render::Lattice::Hex : render::Lattice::Square;
    if (lattice != view_.lattice) fitView();
    return true;
}

void App::fitView() {
    if (!sim_) return;
    if (is3D()) {
        orbit_.fit(sim_->spec().width, sim_->spec().height, sim_->spec().depth);
        return;
    }
    view_.lattice = sim_->rule().neighbourhood.type == rule::NeighbourhoodType::Hexagonal
                        ? render::Lattice::Hex : render::Lattice::Square;
    view_.fit(sim_->spec().width, sim_->spec().height, viewport_);
}

}  // namespace aether::ui
