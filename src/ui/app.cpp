#include "ui/app.hpp"

#include "sim/pattern_library.hpp"

#include "core/gl.hpp"
#include "rule/compile.hpp"
#include "sim/fill.hpp"
#include "sim/session.hpp"

#include <imgui.h>
#include <raylib.h>
#include <rlgl.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <format>

namespace aether::ui {

namespace {

constexpr float kPanelWidth = 380.0f;
// The transport strip spans the window above everything else, so pause,
// step and rate never scroll away behind the panel's other sections.
constexpr float kTransportHeight = 46.0f;

}  // namespace

App::GlWindow::GlWindow(int width, int height, const char* title) {
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(width, height, title);
    ready_ = IsWindowReady();
}

App::GlWindow::~GlWindow() {
    if (ready_) CloseWindow();
}

std::vector<std::string> ruleSearchPath() {
    const std::string exeDir = GetApplicationDirectory();
    const char* env = std::getenv("AETHER_RULES");
    return {env ? env : "", "rules", exeDir + "rules", exeDir + "../rules"};
}

std::vector<std::string> patternSearchPath() {
    const std::string exeDir = GetApplicationDirectory();
    const char* env = std::getenv("AETHER_PATTERNS");
    return {env ? env : "", "patterns", exeDir + "patterns", exeDir + "../patterns"};
}

int App::run() {
    window_.emplace(opts_.windowWidth, opts_.windowHeight, "Aether");
    if (!window_->ready()) return 1;
    if (opts_.screensaver) {
        // The monitor can only be asked once there is a window on it, so the
        // size is set here rather than through a config flag: fullscreen at
        // the default window size is a small picture stretched over a screen.
        const int monitor = GetCurrentMonitor();
        const int mw = GetMonitorWidth(monitor), mh = GetMonitorHeight(monitor);
        if (mw > 0 && mh > 0) SetWindowSize(mw, mh);
        if (!IsWindowFullscreen()) ToggleFullscreen();
    }
    int exitCode = 0;
    {
        rlImGuiSetup(true);
        // ImGui's default font is ProggyClean, which carries Basic Latin and
        // Latin-1 and little else: it has no em dash (U+2014) and no bullet
        // (U+2022), so either drew as a question mark wherever the interface
        // used one (IMP-009). Widening the atlas's glyph range does not help,
        // because the glyphs are not in the font to be rasterised — probed
        // rather than assumed. Pointing each at a glyph the font does have
        // fixes every string at once, including the ones not written yet,
        // which editing seven strings would not: an en dash reads as a dash
        // and a middle dot reads as a separator, and both are what the text
        // meant anyway.
        if (ImGuiIO& io = ImGui::GetIO(); !io.Fonts->Fonts.empty()) {
            ImFont* font = io.Fonts->Fonts[0];
            font->AddRemapChar(0x2014, 0x2013);   // em dash  -> en dash
            font->AddRemapChar(0x2022, 0x00B7);   // bullet   -> middle dot
        }
        // Keep the layout ImGui remembers out of whatever directory the
        // binary was launched from, which is where it lands by default.
        iniPath_ = configDirectory() + "/imgui.ini";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(iniPath_).parent_path(), ec);
        ImGui::GetIO().IniFilename = iniPath_.c_str();

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

        layOut();
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
        library_ = rule::loadLibrary(ruleSearchPath());
        if (!library_.empty()) log_.info(std::format("{} rules in the library", library_.size()));
        patternLibrary_ = sim::loadPatternLibrary(patternSearchPath());
        if (!patternLibrary_.empty()) {
            log_.info(std::format("{} patterns in the library", patternLibrary_.size()));
        }
        if (exitCode == 0 && !opts_.pattern.empty()) {
            std::strncpy(patternPath_.data(), opts_.pattern.c_str(), patternPath_.size() - 1);
            std::ifstream in(opts_.pattern, std::ios::binary);
            if (!in) {
                log_.error(std::format("cannot read {}", opts_.pattern));
            } else {
                std::stringstream ss;
                ss << in.rdbuf();
                auto parsed = sim::parsePattern(ss.str());
                if (const auto* e = std::get_if<sim::PatternError>(&parsed)) log_.error("pattern: " + e->message);
                else setPending(std::get<sim::Pattern>(std::move(parsed)));
            }
        }
        if (exitCode == 0 && !opts_.load.empty()) {
            loadSessionFrom(opts_.load);
            if (!sim_) exitCode = 1;
        } else if (exitCode == 0) {
            ctx_.dimensions = opts_.dimensions;
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
                // A 1D run starts from one cell rather than a soup: that is
                // the picture an elementary rule is known by, and the whole
                // of what makes rule 90 a triangle rather than a mess. Seed
                // is one press away for the other kind (F-005).
                if (sim_->spec().dimensions == 1) seedSingleCell();
                else sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
                sim_->scheduler().setPaused(false);
            }
        }

        if (opts_.screensaver) {
            if (library_.empty()) {
                log_.error("screensaver: no rules in the library");
                exitCode = 1;
            } else {
                HideCursor();
                PlaylistOptions po;
                po.seconds = opts_.screensaverSeconds;
                playlist_.emplace(library_.size(), opts_.seed, po);
                advanceScreensaver();
            }
        }

        int frames = 0;
        while (exitCode == 0 && !WindowShouldClose()) {
            const double dt = GetFrameTime();
            if (opts_.screensaver) {
                // Going fullscreen moves the cursor in the window's frame, so
                // the first moments cannot be taken as input; the baseline is
                // whatever it has settled to by the end of that grace period.
                screensaverAge_ += dt;
                if (!mouseAtStart_) {
                    if (screensaverAge_ > 0.75) {
                        const Vector2 m = GetMousePosition();
                        mouseAtStart_ = std::pair{m.x, m.y};
                    }
                } else if (screensaverInterrupted()) {
                    break;
                }
                entryElapsed_ += dt;
                if (playlist_ && entryElapsed_ >= playlist_->options().seconds) advanceScreensaver();
            }
            layOut();
            if (IsWindowResized()) {
                fitView();
                if (is1D()) rebuildSpaceTime();   // the row count is the viewport's
            }

            updateCanvas(dt);
            if (sim_ && opts_.screensaver && is3D()) {
                // A volume that never turns reads as a photograph. Slow
                // enough that it is not the thing being watched.
                orbit_.yaw += dt * 0.12;
            }
            if (sim_) {
                // Some drivers (Mesa iris) defer the vsync throttle to the
                // first GL call after the swap. Take that wait here, so the
                // scheduler's wall-clock budget measures stepping and not
                // the previous frame's presentation.
                glFinish();
                if (recording_) {
                    // A sequence is specified in generations, so it steps by
                    // generations. Letting the scheduler decide would tie the
                    // record to how fast this machine happened to be drawing.
                    const uint64_t steps = recording_->stepsBefore(sim_->generation());
                    for (uint64_t i = 0; i < steps; ++i) {
                        sim_->step();
                        if (spaceTime_) spaceTime_->capture(sim_->texture());
                    }
                } else if (spaceTime_) {
                    // Every generation is a row, not just the last one of the
                    // frame, or the diagram would have gaps wherever the rate
                    // ran ahead of the frame rate.
                    sim_->frame(dt, [&] { spaceTime_->capture(sim_->texture()); });
                } else {
                    sim_->frame(dt);
                }
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
            if (sim_ && spaceTime_ && renderer_) {
                renderer_->setDecayFrom(std::nullopt);
                spaceTime_->draw(*renderer_, viewport_, GetRenderWidth(), GetRenderHeight(),
                                 sim_->rule().states, static_cast<double>(std::max(1, spaceTimeZoom_)));
            } else if (sim_ && is3D() && renderer3d_) {
                renderer3d_->draw(sim_->texture(), sim_->spec(), orbit_, volumeSettings(), viewport_,
                                  GetRenderWidth(), GetRenderHeight());
            } else if (sim_ && renderer_) {
                // A continuous rule has no state count; the palette is read as a
                // ramp of its full width instead (SPEC §13).
                const unsigned int ramp = sim_->spec().cell_type == core::CellType::F32
                                              ? 256u : sim_->rule().states;
                renderer_->draw(sim_->texture(), sim_->spec(), view_, viewport_,
                                GetRenderWidth(), GetRenderHeight(), ramp);
            }
            // Export before the preview and the panels: what is wanted is the
            // automaton, not the interface around it (F-021).
            if (exportRequested_) {
                exportRequested_ = false;
                const std::string path = exportPath_[0] != '\0' ? exportPath_.data() : "aether.png";
                if (captureViewport(path)) log_.info(std::format("wrote {}", path));
                else log_.error(std::format("cannot write {}", path));
            }
            if (recording_ && sim_) recordingCapture();

            // The preview's cells are a GL pass like the grid's, so they go
            // after it and before ImGui; its outline is an ImGui rectangle and
            // goes with the rest of them.
            drawPatternPreviewCells();
            if (!opts_.screensaver) {
                rlImGuiBegin();
                drawPatternPreview();   // background draw list: behind the panels, over the grid
                drawSelection();
                drawPanels();
                rlImGuiEnd();
            }

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

        // ImGui is shut down here rather than left to a destructor because it
        // writes its layout through `iniPath_`, which is a member declared
        // below the GL objects and therefore destroyed before them.
        rlImGuiShutdown();
    }
    // No resets: `window_` is declared above every GL-owning member, so they
    // are all destroyed before it is (IMP-010). The window outlives `run()`
    // by as long as the App does, which is until the caller drops it.
    return exitCode;
}

// --- Frame export (F-021) ----------------------------------------------------

bool App::captureViewport(const std::string& path) {
    // The batch has to reach the framebuffer before it can be read back.
    rlDrawRenderBatchActive();
    Image shot = LoadImageFromScreen();
    if (shot.data == nullptr) return false;

    // `viewport_` is in window coordinates and the framebuffer may be larger
    // on a scaled display, so the crop is scaled rather than assumed equal.
    const float sx = static_cast<float>(shot.width) / static_cast<float>(GetScreenWidth());
    const float sy = static_cast<float>(shot.height) / static_cast<float>(GetScreenHeight());
    ImageCrop(&shot, Rectangle{viewport_.x * sx, viewport_.y * sy,
                               viewport_.w * sx, viewport_.h * sy});
    // ExportImage writes the path as given. TakeScreenshot does not — it
    // prefixes raylib's base directory and mangles an absolute path — which
    // is why the scripted screenshot has to cd first and this does not.
    const bool ok = ExportImage(shot, path.c_str());
    UnloadImage(shot);
    return ok;
}

void App::recordingCapture() {
    const uint64_t generation = sim_->generation();
    if (recording_->wants(generation) && generation != recording_->lastCaptured) {
        const std::string path = recording_->pathFor(generation);
        if (!captureViewport(path)) {
            log_.error(std::format("cannot write {}; recording stopped", path));
            recording_.reset();
            return;
        }
        recording_->lastCaptured = generation;
        ++recording_->written;
    }
    if (recording_->finished(generation)) {
        log_.info(std::format("wrote {} frames to {}", recording_->written, recording_->dir));
        recording_.reset();
    }
}


// --- Screensaver (F-024) -----------------------------------------------------

void App::advanceScreensaver() {
    if (!playlist_ || library_.empty()) return;
    const PlaylistEntry entry = playlist_->next();
    const rule::LibraryRule& chosen = library_[entry.rule % library_.size()];

    auto ir = rule::compileLibraryRule(chosen, ctx_.boundary);
    if (const auto* bad = std::get_if<std::string>(&ir)) {
        log_.error(std::format("screensaver: {}: {}", chosen.id, *bad));
        return;
    }
    const rule::RuleIR& compiled = std::get<rule::RuleIR>(ir);

    // Shaped like the screen, not square: a square grid fitted to a wide
    // monitor is mostly black, and the point of the mode is what fills it.
    const uint32_t screenW = static_cast<uint32_t>(std::max(1, GetScreenWidth()));
    const uint32_t screenH = static_cast<uint32_t>(std::max(1, GetScreenHeight()));
    constexpr uint32_t kPixelsPerCell = 3;
    uint32_t width = std::clamp(screenW / kPixelsPerCell, 256u, 1536u);
    uint32_t height = std::max(64u, width * screenH / screenW);
    if (compiled.dimensions == 3) { width = height = 64u; }         // a volume, not a plane
    if (compiled.dimensions == 1) { width = std::max(256u, screenW / 2u); height = 1u; }

    // The entry's own seeds, so that what is on screen is a run somebody
    // could ask for again by name and number.
    opts_.seed = entry.seedA;
    opts_.seedB = entry.seedB;
    cellMutationOn_ = entry.cellMutationP > 0.0;
    cellMutationLog_ = static_cast<float>(std::log10(std::max(1e-9, entry.cellMutationP)));
    ruleMutationOn_ = entry.ruleMutation;
    ruleInterval_ = static_cast<int>(entry.ruleInterval);
    ruleMagnitude_ = static_cast<int>(entry.ruleMagnitude);
    paletteOverrides_ = chosen.palette;

    const sim::Path path = sim_ ? sim_->path() : sim::Path::Gpu;
    sim_.reset();
    if (!createSimulation(width, height, compiled.dimensions == 3 ? width : 1u, compiled, path)) {
        return;
    }
    applyPaletteOverrides(compiled);
    if (compiled.dimensions == 1) {
        seedSingleCell();
        // A space-time diagram is one row per generation, so at the ordinary
        // rate it would spend most of its turn as an empty screen with a
        // sliver at the top. Fast enough to fill in a second or two.
        const double rows = spaceTime_ ? static_cast<double>(spaceTime_->rows()) : 512.0;
        sim_->scheduler().setTargetRate(std::max(120.0, rows * 0.75));
    } else {
        sim_->fillRandom(sim::defaultDensity(compiled));
    }
    sim_->scheduler().setPaused(false);
    entryElapsed_ = 0.0;

    // Printed rather than drawn: the mode has no interface, and this is what
    // makes a run reproducible after the fact (F-024's fourth point).
    std::printf("aether: %s  --rule @%s --seed %llu --seed-b %llu%s%s\n",
                chosen.name.c_str(), chosen.id.c_str(),
                static_cast<unsigned long long>(entry.seedA),
                static_cast<unsigned long long>(entry.seedB),
                entry.ruleMutation
                    ? std::format(" --rule-mutation {}:{}", entry.ruleInterval, entry.ruleMagnitude).c_str()
                    : "",
                entry.cellMutationP > 0.0
                    ? std::format(" --cell-mutation {:.2e}", entry.cellMutationP).c_str() : "");
    std::fflush(stdout);
}

bool App::screensaverInterrupted() const {
    if (GetKeyPressed() != 0) return true;
    for (int b = MOUSE_BUTTON_LEFT; b <= MOUSE_BUTTON_BACK; ++b) {
        if (IsMouseButtonPressed(b)) return true;
    }
    // A nudge, not a drift: a mouse that has settled a pixel off where it was
    // should not end the show.
    if (mouseAtStart_) {
        const Vector2 now = GetMousePosition();
        if (std::abs(now.x - mouseAtStart_->first) > 16.0f ||
            std::abs(now.y - mouseAtStart_->second) > 16.0f) {
            return true;
        }
    }
    return false;
}

// One description of the running rule, used wherever it is shown.
void App::refreshRuleSummary() {
    if (!sim_) return;
    const auto& ir = sim_->rule();
    const auto& compiled = sim_->compiled();
    // The hash belongs on a rule's card, not in the middle of a line that
    // then wraps. It is a tooltip.
    ruleHash_ = std::format("{:#018x}", compiled.ir_hash);
    ruleName_ = ir.metadata.name.value_or(ir.metadata.source_notation.value_or(std::string(rule::toString(ir.kind))));
    // A continuous rule has no states to count, so it says what it does have.
    const std::string subject =
        ir.cell_type == core::CellType::F32
            ? std::format("f32 · kernel r{}", ir.neighbourhood.radius)
            : std::format("{} states{}", ir.states,
                          ir.metadata.decay_from
                              ? std::format(" ({} live, decay {})", *ir.metadata.decay_from,
                                            ir.states - *ir.metadata.decay_from)
                              : std::string{});
    ruleSummary_ = std::format("{} · {} · N={} · {} · {}",
                               rule::toString(ir.kind), subject,
                               compiled.neighbourCount(), rule::toString(ir.boundary),
                               compiled.backend == rule::Backend::Codegen
                                   ? std::string("codegen")
                                   : std::format("table {}", compiled.table.size()));
}

// Somewhere per-user to keep interface state: the XDG location if the
// environment names one, the conventional fallback otherwise.
std::string App::configDirectory() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
        return std::string(xdg) + "/aether";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::string(home) + "/.config/aether";
    }
    return ".";
}

// Where the three regions sit. Called every frame so a resize is free.
void App::layOut() {
    const float w = static_cast<float>(GetScreenWidth());
    const float h = static_cast<float>(GetScreenHeight());
    if (opts_.screensaver) {
        // No chrome at all: the automaton is the whole screen (F-024).
        panelRect_ = render::Rect{0.0f, 0.0f, 0.0f, 0.0f};
        viewport_  = render::Rect{0.0f, 0.0f, w, h};
        return;
    }
    panelRect_ = render::Rect{0.0f, kTransportHeight, kPanelWidth, h - kTransportHeight};
    viewport_  = render::Rect{kPanelWidth, kTransportHeight, w - kPanelWidth, h - kTransportHeight};
}

// The window says what is loaded, so a second instance is tellable from a
// first without reading the panel.
void App::refreshWindowTitle() {
    if (!sim_) return;
    const auto& ir = sim_->rule();
    const auto& spec = sim_->spec();
    const std::string name = ir.metadata.name.value_or(ir.metadata.source_notation.value_or("rule"));
    SetWindowTitle(std::format("Aether — {} — {}×{}{}", name.substr(0, 40), spec.width, spec.height,
                               spec.dimensions == 3 ? std::format("×{}", spec.depth) : "")
                       .c_str());
}

bool App::is1D() const { return sim_ && sim_->spec().dimensions == 1; }

void App::seedSingleCell() {
    if (!sim_) return;
    sim_->clear();
    const uint32_t middle = sim_->spec().width / 2;
    sim_->paintSpan(middle, middle, 0, 0, 1);
    rebuildSpaceTime();
}

void App::rebuildSpaceTime() {
    spaceTime_.reset();
    if (!is1D()) return;
    const uint32_t zoom = static_cast<uint32_t>(std::max(1, spaceTimeZoom_));
    const uint32_t rows = std::max(1u, static_cast<uint32_t>(viewport_.h) / zoom);
    auto made = render::SpaceTime::create(sim_->spec().width, rows);
    if (const auto* e = std::get_if<core::Error>(&made)) {
        log_.error(std::format("space-time view: {}", e->message));
        return;
    }
    spaceTime_.emplace(std::move(std::get<render::SpaceTime>(made)));
    // The state as it stands is the first generation of the diagram.
    spaceTime_->capture(sim_->texture());
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
    // The grid's storage follows the rule: a continuous rule needs float
    // cells, and Simulation refuses the pair if they disagree.
    // The rule decides the dimensionality, not the extents: an elementary
    // rule is 1D whatever numbers it was handed (F-005), and a 1D grid is a
    // different thing from a 2D one that happens to be one cell tall.
    if (ir.dimensions < 3) depth = 1;
    if (ir.dimensions < 2) height = 1;
    core::GridSpec spec{ir.dimensions, width, height, depth, ir.cell_type};
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
    rebuildSpaceTime();
    refreshWindowTitle();
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
    const auto defaults = sim::defaultDensity(ir);
    density_.assign(defaults.begin(), defaults.end());
    fitView();
    rebuildSpaceTime();
    refreshWindowTitle();
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
    const auto defaults = sim::defaultDensity(sim_->rule());
    density_.assign(defaults.begin(), defaults.end());
    (void)states;
}

// A rule's own palette, laid over the default for its state count (SPEC §13).
void App::applyPaletteOverrides(const rule::RuleIR& ir) {
    if (!renderer_) return;
    render::Palette pal = ir.cell_type == core::CellType::F32
                              ? render::Palette::continuousRamp()
                              : render::Palette::defaultFor(ir.states, ir.metadata.decay_from);
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
        return named(std::get<rule::RuleIR>(std::move(r)));
    }
    auto parsed = rule::parseDsl(ruleText_.data(), ctx_);
    if (!parsed) {
        ruleError_ = std::format("{}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message);
        return std::nullopt;
    }
    return named(*parsed.ir);
}

// A rule file's header names it, and that header is part of the text in the
// editor, so the name follows the text rather than the way it was loaded.
rule::RuleIR App::named(rule::RuleIR ir) const {
    const rule::LibraryRule header = rule::parseRuleFile("", ruleText_.data(), ruleLanguage_ == 1);
    if (!header.name.empty() && header.name != "") ir.metadata.name = header.name;
    return ir;
}

bool App::compileRuleText() {
    auto compiled = compileRuleSource();
    if (!compiled) {
        log_.error("rule: " + ruleError_);
        return false;
    }
    if (!sim_) return false;
    // A rule of another dimensionality needs a grid of that shape; `setRule`
    // rightly refuses the mismatch rather than reinterpreting the cells, so
    // the grid is rebuilt here as it is for a library rule. An elementary
    // rule is the common way to arrive at this (F-005).
    if (compiled->dimensions != sim_->spec().dimensions) {
        const uint32_t width = compiled->dimensions == 3 ? 64u
                             : compiled->dimensions == 1 ? std::max(64u, sim_->spec().width)
                                                         : 512u;
        const uint32_t height = compiled->dimensions >= 2 ? width : 1u;
        const uint32_t depth  = compiled->dimensions == 3 ? width : 1u;
        const sim::Path path = sim_->path();
        const uint8_t was = sim_->spec().dimensions;
        sim_.reset();
        if (!createSimulation(width, height, depth, *compiled, path)) return false;
        sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
        ctx_.dimensions = compiled->dimensions;
        newWidth_ = static_cast<int>(width);
        newHeight_ = static_cast<int>(height);
        newDepth_ = static_cast<int>(depth);
        ruleError_.clear();
        refreshRuleSummary();
        log_.info(std::format("rule is {}D; rebuilt the grid from {}D", compiled->dimensions, was));
        return true;
    }
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
