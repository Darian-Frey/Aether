#include "ui/app.hpp"

#include "rule/lut.hpp"

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include <cmath>
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

        viewport_ = render::Rect{kPanelWidth, 0.0f,
                                 static_cast<float>(GetScreenWidth()) - kPanelWidth,
                                 static_cast<float>(GetScreenHeight())};
        std::strncpy(ruleText_.data(), opts_.rule.c_str(), ruleText_.size() - 1);
        newWidth_ = static_cast<int>(opts_.width);
        newHeight_ = static_cast<int>(opts_.height);
        targetGpsLog_ = static_cast<float>(std::log10(std::max(0.1, opts_.targetGps)));

        if (exitCode == 0) {
            auto parsed = rule::parseDsl(ruleText_.data(), ctx_);
            if (!parsed) {
                log_.error(std::format("initial rule: {}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message));
                parsed = rule::parseDsl("B3/S23", ctx_);
            }
            if (!createSimulation(opts_.width, opts_.height, *parsed.ir, opts_.cpu ? sim::Path::Cpu : sim::Path::Gpu)) {
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
            if (sim_) sim_->frame(dt);

            BeginDrawing();
            ClearBackground(Color{18, 18, 22, 255});
            if (sim_ && renderer_) {
                renderer_->draw(sim_->texture(), sim_->spec(), view_, viewport_,
                                GetRenderWidth(), GetRenderHeight(), sim_->rule().states);
            }
            rlImGuiBegin();
            drawPanels();
            rlImGuiEnd();
            EndDrawing();

            if (opts_.exitAfterFrames > 0 && ++frames >= opts_.exitAfterFrames) {
                if (!opts_.screenshot.empty()) TakeScreenshot(opts_.screenshot.c_str());
                break;
            }
        }

        rlImGuiShutdown();
        sim_.reset();
        renderer_.reset();
    }
    CloseWindow();
    return exitCode;
}

bool App::createSimulation(uint32_t width, uint32_t height, const rule::RuleIR& ir, sim::Path path) {
    core::GridSpec spec{2, width, height, 1, core::CellType::U8};
    auto made = sim::Simulation::create(spec, ir, path, opts_.seed, opts_.seedB);
    if (const auto* e = std::get_if<core::Error>(&made)) {
        log_.error(std::format("grid {}x{}: {}", width, height, e->message));
        return false;
    }
    sim_.emplace(std::get<sim::Simulation>(std::move(made)));
    sim_->scheduler().setTargetRate(std::pow(10.0, targetGpsLog_));
    sim_->setCellMutation(cellMutationOn_ ? std::pow(10.0, cellMutationLog_) : 0.0);
    ruleSummary_ = std::format("{} · {} states · N={} · {} · table {} · {}",
                               rule::toString(ir.kind), ir.states, sim_->lut().neighbourCount(),
                               rule::toString(ir.boundary), sim_->lut().table.size(),
                               std::format("{:#018x}", sim_->lut().ir_hash));
    log_.info(std::format("grid {}x{} on {} path; rule {}", width, height,
                          path == sim::Path::Gpu ? "GPU" : "CPU", ir.metadata.source_notation.value_or("?")));
    applyPaletteForStates();
    fitView();
    return true;
}

void App::applyPaletteForStates() {
    if (!sim_ || !renderer_) return;
    const uint16_t states = sim_->rule().states;
    renderer_->setPalette(render::Palette::defaultFor(states));
    density_.assign(states - 1u, 0.0f);
    density_[0] = states == 2 ? 0.3f : 0.2f;
    for (size_t i = 1; i < density_.size(); ++i) density_[i] = 0.1f;
}

bool App::compileRuleText() {
    auto parsed = rule::parseDsl(ruleText_.data(), ctx_);
    if (!parsed) {
        ruleError_ = std::format("{}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message);
        log_.error("rule: " + ruleError_);
        return false;
    }
    if (!sim_) return false;
    const uint16_t oldStates = sim_->rule().states;
    if (auto e = sim_->setRule(*parsed.ir)) {
        ruleError_ = e->message;
        log_.error("rule: " + ruleError_);
        return false;
    }
    ruleError_.clear();
    const auto& ir = sim_->rule();
    ruleSummary_ = std::format("{} · {} states · N={} · {} · table {} · {:#018x}",
                               rule::toString(ir.kind), ir.states, sim_->lut().neighbourCount(),
                               rule::toString(ir.boundary), sim_->lut().table.size(), sim_->lut().ir_hash);
    log_.info(std::format("compiled {} -> {} backend, {} entries",
                          ir.metadata.source_notation.value_or("rule").substr(0, 40),
                          rule::selectBackend(ir) == rule::Backend::Lut ? "table" : "codegen",
                          sim_->lut().table.size()));
    if (ir.states != oldStates) applyPaletteForStates();
    if (brush_.state >= ir.states) brush_.state = static_cast<uint8_t>(ir.states - 1);
    return true;
}

void App::fitView() {
    if (!sim_) return;
    view_.fit(sim_->spec().width, sim_->spec().height, viewport_);
}

}  // namespace aether::ui
