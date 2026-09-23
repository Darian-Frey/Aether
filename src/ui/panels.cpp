// Dear ImGui panels.

#include "ui/app.hpp"

#include "rule/compile.hpp"
#include "sim/fill.hpp"
#include "sim/session.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include "sim/pattern.hpp"
#include <imgui.h>
#include <raylib.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>

namespace aether::ui {

namespace {

constexpr float kPanelWidth = 380.0f;
constexpr float kTransportHeight = 46.0f;
// Widgets stop this far short of the right edge so their labels have room.
// Everything clipped before this existed.
constexpr float kLabelColumn = 118.0f;

}  // namespace

namespace {

constexpr ImGuiWindowFlags kFixedPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

// A label the mouse can rest on for more than the label says.
void hint(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

}  // namespace

// Pause, step, burst and rate, spanning the window so they are always to
// hand: they are most of what anyone touches.
void App::drawTransportBar() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(GetScreenWidth()), kTransportHeight), ImGuiCond_Always);
    ImGui::Begin("##transport", nullptr, kFixedPanel | ImGuiWindowFlags_NoScrollbar);
    if (!sim_) { ImGui::End(); return; }

    auto& sch = sim_->scheduler();
    ImGui::AlignTextToFramePadding();

    const bool paused = sch.paused();
    if (ImGui::Button(paused ? "Play" : "Pause", ImVec2(64, 0))) sch.setPaused(!paused);
    hint("Space");
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(48, 0))) sch.requestSingleStep();
    hint("N — advance one generation, paused or not");
    ImGui::SameLine();
    if (ImGui::Button("Burst", ImVec2(52, 0))) sch.requestBurst(static_cast<uint64_t>(std::max(1, burstCount_)));
    hint("Run this many generations as fast as the machine allows, then stop");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72);
    ImGui::InputInt("##burst", &burstCount_, 0);
    if (sch.burstRemaining() > 0) {
        ImGui::SameLine();
        ImGui::Text("%llu left", static_cast<unsigned long long>(sch.burstRemaining()));
        ImGui::SameLine();
        if (ImGui::SmallButton("cancel")) sch.cancelBurst();
    }

    ImGui::SameLine(0, 24);
    ImGui::TextUnformatted("Rate");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (ImGui::SliderFloat("##rate", &targetGpsLog_, -1.0f, 4.0f,
                           std::format("{:.3g} gen/s", std::pow(10.0, targetGpsLog_)).c_str())) {
        sch.setTargetRate(std::pow(10.0, targetGpsLog_));
    }
    hint("Generations per second, independent of frame rate");

    ImGui::SameLine(0, 24);
    const auto& st = sch.stats();
    ImGui::Text("gen %llu", static_cast<unsigned long long>(sim_->generation()));
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("%.0f gen/s · %d fps", st.achieved_gps, GetFPS());
    if (st.below_target) {
        ImGui::SameLine(0, 12);
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "below target");
        hint("The machine cannot keep up; the step cap has been reduced to keep the window responsive");
    }

    // The rule's name, right-aligned, so what is running is never in doubt.
    const std::string label = std::format("{}  ·  {}", ruleName_.substr(0, 36),
                                          sim_->backend() == rule::Backend::Lut ? "table" : "codegen");
    const float width = ImGui::CalcTextSize(label.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), static_cast<float>(GetScreenWidth()) - width - 16.0f));
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::End();
}

void App::drawPanels() {
    drawTransportBar();
    drawViewportOverlay();

    ImGui::SetNextWindowPos(ImVec2(panelRect_.x, panelRect_.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelRect_.w, panelRect_.h), ImGuiCond_Always);
    ImGui::Begin("Aether", nullptr, kFixedPanel);
    // Leave room for every label, which is what used to be clipped.
    ImGui::PushItemWidth(-kLabelColumn);

    // Ordered by when a session needs them, and closed by default past the
    // first two so that the column fits on one screen.
    if (ImGui::CollapsingHeader("Rule", ImGuiTreeNodeFlags_DefaultOpen)) drawRulePanel();
    if (!library_.empty() && ImGui::CollapsingHeader("Library", ImGuiTreeNodeFlags_DefaultOpen)) drawLibraryPanel();
    if (!is3D() && ImGui::CollapsingHeader("Patterns",
                                           (pending_ || !patternLibrary_.empty()) ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        drawPatternsPanel();
    }
    if (ImGui::CollapsingHeader("Grid")) drawGridPanel();
    if (is3D() && ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) drawViewPanel();
    if (ImGui::CollapsingHeader("Brush")) drawBrushPanel();
    if (ImGui::CollapsingHeader("Mutation")) drawMutationPanel();
    if (ImGui::CollapsingHeader("Lineage")) drawLineagePanel();
    if (ImGui::CollapsingHeader("Palette")) drawPalettePanel();
    if (ImGui::CollapsingHeader("Export", recording_ ? ImGuiTreeNodeFlags_DefaultOpen : 0)) drawExportPanel();
    if (ImGui::CollapsingHeader("Session")) drawSessionPanel();
    if (ImGui::CollapsingHeader("Engine")) drawSimulationPanel();
    if (ImGui::CollapsingHeader("Keys", showHelp_ ? ImGuiTreeNodeFlags_DefaultOpen : 0)) drawHelpPanel();
    if (ImGui::CollapsingHeader("Log")) drawLogPanel();

    ImGui::PopItemWidth();
    ImGui::End();

    // Its own window, after the fixed column so it floats over the viewport.
    drawEditor();
}

// What the cursor is over, and whether time is passing. Drawn over the
// viewport's corner, taking no input.
void App::drawViewportOverlay() {
    if (!sim_) return;
    ImGui::SetNextWindowPos(ImVec2(viewport_.x + 12.0f, viewport_.y + viewport_.h - 12.0f),
                            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##overlay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                 ImGuiWindowFlags_NoSavedSettings);

    const auto& spec = sim_->spec();
    if (sim_->scheduler().paused()) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "PAUSED");
        ImGui::SameLine(0, 12);
    }
    const Vector2 m = GetMousePosition();
    const bool over = m.x >= viewport_.x && m.y >= viewport_.y &&
                      m.x < viewport_.x + viewport_.w && m.y < viewport_.y + viewport_.h;
    if (over && !is3D()) {
        if (const auto cell = view_.cellAt(m.x, m.y, viewport_, spec.width, spec.height)) {
            const auto cx = static_cast<uint32_t>(cell->first);
            const auto cy = static_cast<uint32_t>(cell->second);
            // A continuous cell holds a value, and the u8 accessor would read
            // one byte of its four.
            if (spec.cell_type == core::CellType::F32) {
                ImGui::Text("(%d, %d) = %.3f", cell->first, cell->second, sim_->host().getFloat(cx, cy));
            } else {
                ImGui::Text("(%d, %d) = %u", cell->first, cell->second, sim_->host().get(cx, cy));
            }
        } else {
            ImGui::TextDisabled("outside the grid");
        }
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("%.0f×", view_.zoom);
    } else if (over && is3D()) {
        ImGui::Text("%s", sliceMode_ ? "slice mode — left drag paints" : "right drag to orbit");
    }
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("brush %u, r%d", brush_.state, brush_.radius);
    ImGui::End();
}

void App::drawHelpPanel() {
    static const std::pair<const char*, const char*> keys[] = {
        {"Space", "pause or resume"},
        {"N", "one generation"},
        {"R / C", "random fill / clear"},
        {"F", "fit the grid to the view"},
        {"0–9", "choose the brush state"},
        {"[ / ]", "brush radius"},
        {"Ctrl+Enter", "compile the rule"},
        {"Left drag", "paint"},
        {"Shift+drag", "select a region"},
        {"Left click", "place a pending pattern"},
        {"Esc", "cancel a pending pattern"},
        {"E", "the pattern editor's scratch pad"},
        {"Right drag", "pan (2D) or orbit (3D)"},
        {"Wheel", "zoom"},
        {"S", "3D: slice mode"},
        {", / .", "3D: move the slice"},
    };
    if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& [key, what] : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", what);
        }
        ImGui::EndTable();
    }
}

void App::drawRulePanel() {
    ImGui::PushID("rule");
    ImGui::SetNextItemWidth(90);
    ImGui::Combo("##lang", &ruleLanguage_, "DSL\0Lua\0");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lua scripts run once, at compile time, and return a rule table");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ruleLanguage_ == 1 ? "a script returning a rule table (SPEC §8)"
                                                 : "B3/S23, B2/S/C3, or a table block");
    ImGui::InputTextMultiline("##src", ruleText_.data(), ruleText_.size(),
                              ImVec2(-1, ruleLanguage_ == 1 ? 200 : 96),
                              ImGuiInputTextFlags_AllowTabInput);
    bool apply = ImGui::Button("Compile");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Enter");
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter)) apply = true;
    if (apply) compileRuleText();

    int boundary = static_cast<int>(ctx_.boundary);
    if (ImGui::Combo("Boundary", &boundary, "wrap\0zero\0mirror\0")) {
        ctx_.boundary = static_cast<rule::Boundary>(boundary);
        compileRuleText();
    }
    if (!ruleError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", ruleError_.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(ruleName_.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", ruleSummary_.c_str());
        ImGui::PopStyleColor();
        hint(("ir_hash " + ruleHash_ + "\nThis is what the lineage and the shader cache identify a rule by").c_str());
    }
    ImGui::PopID();
}

void App::drawSimulationPanel() {
    if (!sim_) return;
    ImGui::PushID("sim");
    auto& sch = sim_->scheduler();

    int path = sim_->path() == sim::Path::Gpu ? 0 : 1;
    ImGui::TextUnformatted("Execution path");
    if (ImGui::RadioButton("GPU", path == 0)) { path = 0; }
    hint("The compute shader: what a run normally uses");
    ImGui::SameLine();
    if (ImGui::RadioButton("CPU reference", path == 1)) { path = 1; }
    hint("The serial oracle. Identical results, far slower — for checking, not for running");
    const sim::Path want = path == 0 ? sim::Path::Gpu : sim::Path::Cpu;
    if (want != sim_->path()) {
        if (auto e = sim_->setPath(want)) log_.error(e->message);
        else log_.info(want == sim::Path::Gpu ? "switched to GPU path" : "switched to CPU reference path");
    }

    int cap = static_cast<int>(sch.maxStepsPerFrame());
    if (ImGui::SliderInt("Max steps", &cap, 1, 1024, "%d", ImGuiSliderFlags_Logarithmic)) {
        sch.setMaxStepsPerFrame(static_cast<uint32_t>(cap));
    }
    hint("Most generations one frame may run. A long frame lowers this by itself\n"
         "so the window stays responsive under an unreachable rate");
    if (sch.stats().effective_cap < sch.maxStepsPerFrame()) {
        ImGui::TextDisabled("currently held at %u", sch.stats().effective_cap);
    }
    ImGui::PopID();
}

void App::drawGridPanel() {
    if (!sim_) return;
    ImGui::PushID("grid");
    ImGui::InputInt("Width", &newWidth_, 16, 256);
    ImGui::InputInt("Height", &newHeight_, 16, 256);
    ImGui::InputInt("Depth", &newDepth_, 16, 64);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 for a 2D grid; more for 3D (the rule must be 3D too)");
    newWidth_ = std::clamp(newWidth_, 1, 16384);
    newHeight_ = std::clamp(newHeight_, 1, 16384);
    newDepth_ = std::clamp(newDepth_, 1, 2048);
    {
        const core::GridSpec want{static_cast<uint8_t>(newDepth_ > 1 ? 3 : 2), static_cast<uint32_t>(newWidth_),
                                  static_cast<uint32_t>(newHeight_), static_cast<uint32_t>(newDepth_)};
        ImGui::TextDisabled("%.1f MB for the pair", static_cast<double>(want.footprintBytes()) / 1048576.0);
    }
    if (ImGui::Button("New grid")) {
        const uint8_t dims = newDepth_ > 1 ? 3 : 2;
        rule::RuleIR ir = sim_->rule();
        if (ir.dimensions != dims) {
            // The rule must match the lattice; re-parse the text for the new
            // dimensionality, falling back to a sensible default.
            rule::DslContext ctx = ctx_;
            ctx.dimensions = dims;
            auto parsed = rule::parseDsl(ruleText_.data(), ctx);
            if (!parsed) parsed = rule::parseDsl(dims == 3 ? "B5/S45" : "B3/S23", ctx);
            ir = *parsed.ir;
            ctx_.dimensions = dims;
            std::strncpy(ruleText_.data(), ir.metadata.source_notation.value_or("").c_str(), ruleText_.size() - 1);
        }
        const sim::Path path = sim_->path();
        sim_.reset();
        if (createSimulation(static_cast<uint32_t>(newWidth_), static_cast<uint32_t>(newHeight_),
                             static_cast<uint32_t>(newDepth_), ir, path)) {
            sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
        }
    }
    ImGui::Separator();

    // The actions first (IMP-004). They are what this panel is for, and they
    // used to sit under sixteen sliders that are touched once a session.
    const std::vector<double> density(density_.begin(), density_.end());
    if (ImGui::Button("Seed")) sim_->fillRandom(density);
    hint("R — fill the whole grid at the densities below");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) sim_->clear();
    hint("C");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) fitView();
    hint("F — whole grid in view, at a zoom where each cell is a whole number of pixels");
    if (!is3D()) {
        ImGui::SameLine();
        if (ImGui::Button("Fill view")) {
            view_.fit(sim_->spec().width, sim_->spec().height, viewport_, false);
        }
        hint("Use the whole viewport, at a fractional zoom. Cells stop being pixel-exact");
    }

    // A 1D run has its own starting point: the single cell an elementary
    // rule's picture is drawn from (F-005). A soup is still a press away.
    if (is1D()) {
        ImGui::SameLine();
        if (ImGui::Button("Single cell")) seedSingleCell();
        hint("one live cell in the middle, which is how an elementary rule is usually read");
    }

    // Seeding part of the grid (F-028). In 2D the part is the shift-dragged
    // selection; in 3D there is no way to drag one, so it is the slice the
    // brush is already painting on, which is what F-011 does there too.
    if (is3D()) {
        if (ImGui::Button("Seed slice")) {
            const uint32_t ext[3] = {sim_->spec().width, sim_->spec().height, sim_->spec().depth};
            uint32_t at[3] = {0, 0, 0}, size[3] = {ext[0], ext[1], ext[2]};
            at[sliceAxis_] = static_cast<uint32_t>(sliceIndex_);
            size[sliceAxis_] = 1;
            sim_->fillRegion(at[0], at[1], at[2], size[0], size[1], size[2], density);
        }
        hint("seed only the slice the brush paints on, at the densities below");
    } else {
        ImGui::BeginDisabled(!selection_);
        if (ImGui::Button("Seed region") && selection_) {
            sim_->fillRegion(selection_->x0, selection_->y0, 0,
                             selection_->x1 - selection_->x0 + 1,
                             selection_->y1 - selection_->y0 + 1, 1, density);
        }
        ImGui::EndDisabled();
        hint(selection_ ? "seed the selected region only, leaving the rest alone"
                        : "shift-drag the grid to select a region first");
        if (selection_) {
            ImGui::SameLine();
            ImGui::TextDisabled("%ux%u", selection_->x1 - selection_->x0 + 1,
                                selection_->y1 - selection_->y0 + 1);
        }
    }

    // The densities last, and folded away: they are set once and then left.
    if (ImGui::TreeNode("Random fill density")) {
        float total = 0.0f;
        for (float d : density_) total += d;
        if (density_.size() > 16) ImGui::TextDisabled("(showing the first 16 of %zu states)", density_.size());
        for (size_t i = 0; i < density_.size() && i < 16; ++i) {
            ImGui::SliderFloat(std::format("state {}", i + 1).c_str(), &density_[i], 0.0f, 1.0f);
        }
        if (total > 1.0f) {
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
                               "densities total %.2f; the last states will not be seeded", total);
        } else {
            ImGui::TextDisabled("state 0 takes the remaining %.2f", 1.0f - total);
        }
        if (ImGui::SmallButton("even spread")) {
            const auto defaults = sim::defaultDensity(sim_->rule());
            density_.assign(defaults.begin(), defaults.end());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void App::drawMutationPanel() {
    if (!sim_) return;
    ImGui::PushID("mutation");
    bool changed = ImGui::Checkbox("Cell mutation", &cellMutationOn_);
    ImGui::SameLine();
    ImGui::TextDisabled("seed B %llu", static_cast<unsigned long long>(sim_->seedB()));
    ImGui::BeginDisabled(!cellMutationOn_);
    changed |= ImGui::SliderFloat("chance", &cellMutationLog_, -7.0f, 0.0f,
                                  std::format("{:.2e}", std::pow(10.0, cellMutationLog_)).c_str());
    changed |= ImGui::SliderInt("block", &cellMutationBlock_, 0, 8,
                                cellMutationBlock_ == 0 ? "one cell" : std::format("{} cells", 1 << cellMutationBlock_).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cells mutate in aligned blocks of this size, so changes arrive in clumps.\n"
                                                  "p stays the expected fraction of cells changed per generation.");
    ImGui::EndDisabled();
    if (changed) {
        sim_->setCellMutation(cellMutationOn_ ? std::pow(10.0, cellMutationLog_) : 0.0,
                              static_cast<uint8_t>(cellMutationBlock_));
    }

    ImGui::Separator();
    bool rchanged = ImGui::Checkbox("Rule mutation", &ruleMutationOn_);
    ImGui::SameLine();
    ImGui::TextDisabled("%llu applied, %llu skipped",
                        static_cast<unsigned long long>(sim_->counters().rule_mutations),
                        static_cast<unsigned long long>(sim_->counters().rule_mutations_skipped));
    ImGui::BeginDisabled(!ruleMutationOn_);
    rchanged |= ImGui::SliderInt("every", &ruleInterval_, 1, 5000, "%d gens", ImGuiSliderFlags_Logarithmic);
    rchanged |= ImGui::SliderInt("edits", &ruleMagnitude_, 1, 32, "%d per event");
    ImGui::EndDisabled();
    if (rchanged) {
        sim_->setRuleMutation({ruleMutationOn_, static_cast<uint32_t>(ruleInterval_), static_cast<uint32_t>(ruleMagnitude_)});
    }
    ImGui::PopID();
}

void App::drawLineagePanel() {
    if (!sim_) return;
    ImGui::PushID("lineage");
    const auto& entries = sim_->lineage().entries();
    ImGui::Text("%zu rules so far; current is #%zu", entries.size(), entries.size() - 1);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pinname", "name for pin", pinName_.data(), pinName_.size());
    ImGui::BeginChild("entries", ImVec2(-1, 180), ImGuiChildFlags_Borders);
    // Newest first.
    for (size_t k = entries.size(); k-- > 0;) {
        const auto& e = entries[k];
        ImGui::PushID(static_cast<int>(k));
        const bool current = k + 1 == entries.size();
        if (current) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
        ImGui::Text("#%zu  gen %llu  %s%s", k, static_cast<unsigned long long>(e.generation),
                    e.name ? e.name->c_str() : std::format("{:#010x}", static_cast<uint32_t>(e.ir_hash)).c_str(),
                    e.rewound_from ? "  (rewind)" : "");
        if (current) ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        if (e.pinned) {
            if (ImGui::SmallButton("unpin")) sim_->unpin(k);
        } else if (ImGui::SmallButton("pin")) {
            const std::string name = pinName_[0] ? std::string(pinName_.data()) : std::format("rule #{}", k);
            sim_->pin(k, name);
            pinName_[0] = '\0';
        }
        if (!current) {
            ImGui::SameLine();
            if (ImGui::SmallButton("rule")) {
                if (auto err = sim_->rewind(k)) log_.error(err->message);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore this rule; keep the grid");
            ImGui::SameLine();
            if (ImGui::SmallButton("grid")) {
                // Time travel: replay to this entry and abandon the future.
                auto made = sim::Simulation::rewindGrid(sim_->session(), k, sim_->path());
                if (const auto* err = std::get_if<core::Error>(&made)) log_.error("rewind: " + err->message);
                else {
                    sim_.reset();
                    adoptSimulation(std::get<sim::Simulation>(std::move(made)), std::format("rewound to #{}", k).c_str());
                    ImGui::PopID();
                    break;   // the entries vector is gone
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replay to this point: grid and rule, journal truncated");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void App::drawViewPanel() {
    if (!sim_) return;
    ImGui::PushID("view");
    const auto& sp = sim_->spec();
    ImGui::SliderFloat("Opacity", &opacity_, 0.0f, 1.0f);
    ImGui::TextUnformatted("Clip");
    const char* axisNames[3] = {"x", "y", "z"};
    for (int a = 0; a < 3; ++a) {
        ImGui::PushID(a);
        float lo = clipLo_[static_cast<size_t>(a)], hi = clipHi_[static_cast<size_t>(a)];
        if (ImGui::DragFloatRange2(axisNames[a], &lo, &hi, 0.002f, 0.0f, 1.0f, "%.2f", "%.2f")) {
            clipLo_[static_cast<size_t>(a)] = std::min(lo, hi);
            clipHi_[static_cast<size_t>(a)] = std::max(lo, hi);
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("reset clip")) { clipLo_ = {0, 0, 0}; clipHi_ = {1, 1, 1}; }
    ImGui::Separator();
    ImGui::Checkbox("Slice mode", &sliceMode_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show one plane; left-drag paints on it");
    ImGui::BeginDisabled(!sliceMode_);
    ImGui::Combo("Axis", &sliceAxis_, "x\0y\0z\0");
    const int ext = static_cast<int>(sliceAxis_ == 0 ? sp.width : sliceAxis_ == 1 ? sp.height : sp.depth);
    sliceIndex_ = std::clamp(sliceIndex_, 0, ext - 1);
    ImGui::SliderInt("index", &sliceIndex_, 0, ext - 1);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Right drag orbits, wheel zooms, F fits.");
    if (ImGui::SmallButton("Fit (F)")) fitView();
    ImGui::PopID();
}

void App::drawBrushPanel() {
    if (!sim_) return;
    ImGui::PushID("brush");
    int state = brush_.state;
    if (ImGui::SliderInt("state", &state, 0, sim_->rule().states - 1)) brush_.state = static_cast<uint8_t>(state);
    ImGui::SliderInt("radius", &brush_.radius, 0, 64);
    ImGui::TextDisabled("Left drag paints. See Keys for the rest.");
    ImGui::PopID();
}

void App::drawPalettePanel() {
    if (!sim_ || !renderer_) return;
    ImGui::PushID("palette");
    bool age = renderer_->ageShading();
    if (ImGui::Checkbox("Age shading", &age)) renderer_->setAgeShading(age);
    render::Palette pal = renderer_->palette();
    bool changed = false;
    for (uint16_t s = 0; s < sim_->rule().states && s < 32; ++s) {
        float c[4] = {pal.entries[s].r / 255.0f, pal.entries[s].g / 255.0f, pal.entries[s].b / 255.0f, pal.entries[s].a / 255.0f};
        if (ImGui::ColorEdit4(std::format("state {}", s).c_str(), c, ImGuiColorEditFlags_NoInputs)) {
            pal.entries[s] = {static_cast<uint8_t>(c[0] * 255), static_cast<uint8_t>(c[1] * 255),
                              static_cast<uint8_t>(c[2] * 255), static_cast<uint8_t>(c[3] * 255)};
            changed = true;
        }
    }
    if (ImGui::Button("Reset palette")) {
        pal = sim_->rule().cell_type == core::CellType::F32
                  ? render::Palette::continuousRamp()
                  : render::Palette::defaultFor(sim_->rule().states, sim_->rule().metadata.decay_from);
        changed = true;
    }
    if (sim_->rule().metadata.decay_from) {
        ImGui::TextDisabled("States %u and up are the ageing tail.", *sim_->rule().metadata.decay_from);
    }
    if (is3D()) ImGui::TextDisabled("Alpha is each state's opacity in the volume.");
    if (changed) {
        renderer_->setPalette(pal);
        if (renderer3d_) renderer3d_->setPalette(pal);
    }
    (void)0;
    ImGui::PopID();
}

void App::drawLibraryPanel() {
    ImGui::PushID("library");
    ImGui::BeginChild("rules", ImVec2(-1, 150), ImGuiChildFlags_Borders);
    for (const rule::LibraryRule& entry : library_) {
        ImGui::PushID(entry.id.c_str());
        if (ImGui::Selectable(entry.name.c_str())) loadLibraryRule(entry);
        if (ImGui::IsItemHovered() && !entry.description.empty()) {
            ImGui::SetTooltip("%s\n\n%s%s", entry.description.c_str(),
                              entry.isLua ? "Lua" : "DSL", entry.dimensions == 3 ? " · 3D" : "");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##saveid", "name to save as", saveRuleId_.data(), saveRuleId_.size());
    ImGui::SameLine();
    if (ImGui::Button("Save rule") && saveRuleId_[0] != '\0' && sim_) {
        rule::LibraryRule entry;
        entry.id = saveRuleId_.data();
        entry.name = entry.id;
        entry.source = ruleText_.data();
        entry.isLua = ruleLanguage_ == 1;
        entry.dimensions = sim_->spec().dimensions;
        if (auto e = rule::saveRule("rules", entry)) {
            log_.error(*e);
        } else {
            log_.info(std::format("saved rules/{}{}", entry.id, entry.isLua ? ".lua" : ".rule"));
            const std::string exeDir = GetApplicationDirectory();
            const char* env = std::getenv("AETHER_RULES");
            library_ = rule::loadLibrary({env ? env : "", "rules", exeDir + "rules", exeDir + "../rules"});
            saveRuleId_[0] = '\0';
        }
    }
    ImGui::PopID();
}

// Frame export (F-021): one PNG of what is on screen, or a numbered sequence
// over a range of generations for something else to encode.
void App::drawExportPanel() {
    if (!sim_) return;
    ImGui::PushID("export");

    ImGui::SetNextItemWidth(-70);
    ImGui::InputTextWithHint("##png", "aether.png", exportPath_.data(), exportPath_.size());
    ImGui::SameLine();
    if (ImGui::Button("PNG")) exportRequested_ = true;
    hint("the viewport as it stands, without the panels over it. Taken at the end of this frame");

    ImGui::Separator();
    if (recording_) {
        const uint64_t total = recording_->totalFrames();
        ImGui::Text("recording %llu of %llu", static_cast<unsigned long long>(recording_->written),
                    static_cast<unsigned long long>(total));
        ImGui::ProgressBar(total == 0 ? 0.0f
                                      : static_cast<float>(recording_->written) / static_cast<float>(total),
                           ImVec2(-1, 0));
        ImGui::TextDisabled("generation %llu of %llu",
                            static_cast<unsigned long long>(sim_->generation()),
                            static_cast<unsigned long long>(recording_->to));
        if (ImGui::Button("Stop")) {
            log_.info(std::format("recording stopped after {} frames", recording_->written));
            recording_.reset();
        }
        hint("keeps the frames already written");
        ImGui::PopID();
        return;
    }

    ImGui::SetNextItemWidth(-kLabelColumn);
    ImGui::InputTextWithHint("folder", "frames", recordDir_.data(), recordDir_.size());
    ImGui::InputInt("from", &recordFrom_, 1, 100);
    ImGui::InputInt("to", &recordTo_, 1, 100);
    ImGui::InputInt("every", &recordEvery_, 1, 10);
    recordFrom_  = std::max(0, recordFrom_);
    recordTo_    = std::max(0, recordTo_);
    recordEvery_ = std::clamp(recordEvery_, 1, 100000);
    ImGui::SameLine();
    if (ImGui::SmallButton("from here")) {
        recordFrom_ = static_cast<int>(sim_->generation());
        recordTo_ = recordFrom_ + 500;
    }

    Recording planned;
    planned.from = static_cast<uint64_t>(recordFrom_);
    planned.to = static_cast<uint64_t>(recordTo_);
    planned.every = static_cast<uint32_t>(recordEvery_);
    planned.dir = recordDir_[0] != '\0' ? recordDir_.data() : "frames";

    const uint64_t frames = planned.totalFrames();
    const bool behind = planned.from < sim_->generation();
    // Wrapped, not just coloured: the panel is narrow and a refusal that runs
    // off its right edge is the same as no refusal at all (BUG-013's lesson).
    if (frames == 0 || behind) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.45f, 1.0f));
        if (frames == 0) {
            ImGui::TextWrapped("no frames: `to` is before `from`");
        } else {
            // Generations only run forwards, so a range already passed cannot
            // be recorded without rewinding to it first (F-017).
            ImGui::TextWrapped("generation %llu is already past. Rewind to it, or press "
                               "'from here' to start where the run is.",
                               static_cast<unsigned long long>(sim_->generation()));
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::TextWrapped("%llu frames, first is %s", static_cast<unsigned long long>(frames),
                           planned.pathFor(planned.from).c_str());
    }

    ImGui::BeginDisabled(frames == 0 || behind);
    if (ImGui::Button("Record")) {
        std::error_code ec;
        std::filesystem::create_directories(planned.dir, ec);
        if (ec) {
            log_.error(std::format("cannot make {}: {}", planned.dir, ec.message()));
        } else {
            recording_ = planned;
            log_.info(std::format("recording {} frames, generations {} to {} every {}",
                                  frames, planned.from, planned.to, planned.every));
        }
    }
    ImGui::EndDisabled();
    hint("steps by generations rather than by frames, so the sequence is the run and not this machine's frame rate");

    ImGui::PopID();
}

void App::drawSessionPanel() {
    if (!sim_) return;
    ImGui::PushID("session");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##path", "path/to/session.aether", sessionPath_.data(), sessionPath_.size());
    if (ImGui::Button("Save")) saveSessionTo(sessionPath_.data());
    ImGui::SameLine();
    if (ImGui::Button("Load")) loadSessionFrom(sessionPath_.data());
    ImGui::SameLine();
    if (ImGui::Button("Verify replay")) verifyReplay();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replay from the initial state on the other path and compare");
    ImGui::TextDisabled("%zu journal events; seeds A %llu, B %llu", sim_->journal().size(),
                        static_cast<unsigned long long>(sim_->seedA()), static_cast<unsigned long long>(sim_->seedB()));
    ImGui::PopID();
}

void App::drawLogPanel() {
    ImGui::PushID("log");
    ImGui::BeginChild("lines", ImVec2(-1, 160), ImGuiChildFlags_Borders);
    for (const auto& line : log_.lines()) ImGui::TextUnformatted(line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopID();
}


// Where a pending pattern would land: centred on the cursor, in cells. Empty
// when the cursor is outside the viewport or there is nothing pending.
std::optional<std::pair<int, int>> App::pendingOrigin() const {
    if (!pending_) return std::nullopt;
    const auto cell = cellUnderCursor();
    if (!cell) return std::nullopt;
    // Centred on the cursor: a pattern is easier to place by its middle than
    // by a corner nobody can see.
    return std::pair{cell->first - static_cast<int>(pending_->width) / 2,
                     cell->second - static_cast<int>(pending_->height) / 2};
}

// Why the pending pattern could not be placed where the cursor is, or
// nothing. Asked of the engine rather than guessed at here, so the preview
// cannot disagree with what the click will do.
std::optional<std::string> App::pendingProblem() const {
    if (!pending_ || !sim_) return std::nullopt;
    const auto origin = pendingOrigin();
    // With the cursor away from the grid there is no position to judge, but a
    // pattern for another rule is wrong wherever it goes — so ask about the
    // origin, which reports everything except the bounds.
    if (!origin) {
        if (auto e = sim_->canPlace(*pending_, 0, 0, 0)) return e->message;
        return std::nullopt;
    }
    if (origin->first < 0 || origin->second < 0) {
        return std::string("it would hang over the edge of the grid");
    }
    if (auto e = sim_->canPlace(*pending_, static_cast<uint32_t>(origin->first),
                                static_cast<uint32_t>(origin->second), 0)) {
        return e->message;
    }
    return std::nullopt;
}

// Drawn, never written: the grid is untouched until the click (invariant 6).
// Cells are filled while there are few enough of them to be worth it, and the
// footprint is outlined either way so a large pattern still shows where it
// goes.
// The pending pattern, drawn over the grid and never into it (F-012).
//
// The cells go through the same palette pass the grid does (IMP-008), so a
// preview shows the states it will actually become rather than a wash of one
// colour, and a five-hundred-square pattern costs the same as a glider. What
// stays in ImGui is the footprint outline, which has to be visible where the
// pattern is empty and so cannot come from the cells.
void App::setPending(std::optional<sim::Pattern> p) {
    pending_ = std::move(p);
    ++pendingSerial_;
}

void App::drawPatternPreviewCells() {
    if (!pending_ || !renderer_ || !sim_ || is3D()) return;
    const auto origin = pendingOrigin();
    if (!origin) return;

    // Re-upload only when the pattern itself changed; holding one under the
    // cursor moves the draw, not the data.
    if (previewSerial_ != pendingSerial_ || !previewGrid_) {
        previewGrid_.reset();
        core::GridSpec spec{2, pending_->width, pending_->height, 1, pending_->cell_type};
        auto made = core::GpuGrid::create(spec, core::queryVram());
        if (const auto* e = std::get_if<core::Error>(&made)) {
            log_.error(std::format("pattern preview: {}", e->message));
            previewSerial_ = pendingSerial_;   // do not retry every frame
            return;
        }
        previewGrid_.emplace(std::move(std::get<core::GpuGrid>(made)));
        previewGrid_->upload(pending_->cells);
        previewSerial_ = pendingSerial_;
    }
    if (!previewGrid_) return;

    const auto [ox, oy] = *origin;
    const bool fits = !pendingProblem().has_value();
    const render::Rgba tint = fits ? render::Rgba{150, 200, 255, 60}
                                   : render::Rgba{235, 110, 85, 150};
    const unsigned int ramp = pending_->cell_type == core::CellType::F32
                                  ? 256u : std::max<unsigned int>(2, pending_->states);
    renderer_->drawOverlay(previewGrid_->current(), previewGrid_->spec(), view_, viewport_,
                           GetRenderWidth(), GetRenderHeight(), ramp,
                           static_cast<double>(ox), static_cast<double>(oy), tint);
}

void App::drawPatternPreview() {
    const auto origin = pendingOrigin();
    if (!origin || !sim_) return;
    const auto [ox, oy] = *origin;
    // Red for *any* reason the click would be refused, not only for hanging
    // over an edge: a pattern for another rule looked identical to one that
    // would place, and the refusal was only a line in a collapsed log.
    const bool fits = !pendingProblem().has_value();

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(viewport_.x, viewport_.y),
                     ImVec2(viewport_.x + viewport_.w, viewport_.y + viewport_.h), true);

    const ImU32 edge = fits ? IM_COL32(150, 200, 255, 220) : IM_COL32(235, 120, 90, 230);
    const auto [x0, y0] = view_.cellToScreen(ox, oy, viewport_);
    const auto [x1, y1] = view_.cellToScreen(ox + static_cast<double>(pending_->width),
                                             oy + static_cast<double>(pending_->height), viewport_);
    dl->AddRect(ImVec2(static_cast<float>(x0), static_cast<float>(y0)),
                ImVec2(static_cast<float>(x1), static_cast<float>(y1)), edge, 0.0f, 0, 1.5f);
    dl->PopClipRect();
}

// The selected region, drawn the same way and for the same reason.
void App::drawSelection() {
    if (!selection_ || !sim_ || is3D()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushClipRect(ImVec2(viewport_.x, viewport_.y),
                     ImVec2(viewport_.x + viewport_.w, viewport_.y + viewport_.h), true);
    const auto [x0, y0] = view_.cellToScreen(selection_->x0, selection_->y0, viewport_);
    const auto [x1, y1] = view_.cellToScreen(selection_->x1 + 1.0, selection_->y1 + 1.0, viewport_);
    const ImVec2 a(static_cast<float>(x0), static_cast<float>(y0));
    const ImVec2 b(static_cast<float>(x1), static_cast<float>(y1));
    dl->AddRectFilled(a, b, IM_COL32(255, 210, 120, 40));
    dl->AddRect(a, b, IM_COL32(255, 210, 120, 220), 0.0f, 0, 1.5f);
    dl->PopClipRect();
}

void App::savePatternSelection() {
    if (!sim_ || !selection_) return;
    const auto& sel = *selection_;
    auto got = sim_->extractPattern(sel.x0, sel.y0, 0, sel.x1 - sel.x0 + 1, sel.y1 - sel.y0 + 1, 1);
    if (const auto* e = std::get_if<core::Error>(&got)) {
        log_.error(e->message);
        return;
    }
    savePatternFile(std::get<sim::Pattern>(std::move(got)), savePatternAs_.data(), "selection");
}

// The one place a pattern is written to a file. The format follows the
// pattern rather than the name (D-017), and `pathFor` decides where a bare
// name lands, so the editor and the selection agree on both without either
// knowing how the other does it.
void App::savePatternFile(sim::Pattern p, std::string name, const char* fallbackName) {
    if (!name.empty()) p.name = name;

    const sim::Format f = sim::formatFor(p);
    auto text = sim::writePattern(p, f);
    if (const auto* e = std::get_if<sim::PatternError>(&text)) {
        log_.error(e->message);
        return;
    }

    const std::string path = sim::pathFor(name.empty() ? fallbackName : name, f);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        log_.error(std::format("cannot write {}", path));
        return;
    }
    out << std::get<std::string>(text);
    log_.info(std::format("wrote {} ({}x{}, {})", path, p.width, p.height,
                          f == sim::Format::Rle ? "extended RLE" : "native"));
}

void App::drawPatternsPanel() {
    ImGui::PushID("patterns");
    if (!patternLibrary_.empty()) {
        ImGui::BeginChild("bundled", ImVec2(-1, 110), ImGuiChildFlags_Borders);
        for (const sim::LibraryPattern& entry : patternLibrary_) {
            ImGui::PushID(entry.id.c_str());
            // Greyed where the running rule cannot take it — a pattern for
            // another rule is still worth picking up to see why, but it should
            // not look like one that will place.
            const bool fits = sim_ && !sim_->canPlace(entry.pattern, 0, 0, 0).has_value();
            if (!fits) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Selectable(entry.name.c_str())) {
                setPending(entry.pattern);
                log_.info(std::format("{} — click the grid to place it", entry.name));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%ux%u · %s%s%s\n\n%s", entry.pattern.width, entry.pattern.height,
                                  std::string(sim::toString(entry.pattern.lattice)).c_str(),
                                  entry.pattern.rule ? " · rule " : "",
                                  entry.pattern.rule ? entry.pattern.rule->c_str() : "",
                                  entry.description.c_str());
            }
            if (!fits) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##path", "path to a .rle or .pattern", patternPath_.data(), patternPath_.size());
    ImGui::SameLine();
    if (ImGui::Button("Open") && patternPath_[0] != '\0') {
        std::ifstream in(patternPath_.data(), std::ios::binary);
        if (!in) {
            log_.error(std::format("cannot read {}", patternPath_.data()));
        } else {
            std::stringstream ss;
            ss << in.rdbuf();
            auto parsed = sim::parsePattern(ss.str());
            if (const auto* e = std::get_if<sim::PatternError>(&parsed)) {
                log_.error(std::format("pattern: {}", e->message));
            } else {
                setPending(std::get<sim::Pattern>(std::move(parsed)));
                log_.info(std::format("{} loaded — click the grid to place it",
                                      pending_->name.value_or(std::string(patternPath_.data()))));
            }
        }
    }

    ImGui::Separator();
    if (selection_) {
        const auto& sel = *selection_;
        ImGui::Text("selection %ux%u at (%u, %u)", sel.x1 - sel.x0 + 1, sel.y1 - sel.y0 + 1, sel.x0, sel.y0);
        ImGui::SetNextItemWidth(-120);
        ImGui::InputTextWithHint("##saveas", "name to save as", savePatternAs_.data(), savePatternAs_.size());
        ImGui::SameLine();
        if (ImGui::Button("Save region")) savePatternSelection();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) selection_.reset();
    } else {
        ImGui::TextDisabled("shift-drag the grid to select a region to save");
    }
    ImGui::Separator();

    // The way into the editor (F-029). It lives with the patterns because
    // that is what it makes: a pad is drawn on, and what leaves it is a
    // pattern, placed or saved like any other.
    if (ImGui::Checkbox("Pattern editor", &showEditor_)) {
        if (showEditor_) ensureScratch();
    }
    hint("E — a scratch pad to draw a creature on and step, outside the run");
    ImGui::Separator();

    if (!pending_) {
        ImGui::TextDisabled("nothing pending");
        ImGui::PopID();
        return;
    }
    ImGui::Text("%s", pending_->name.value_or("(unnamed)").c_str());
    ImGui::TextDisabled("%ux%u · %s · %u states%s%s", pending_->width, pending_->height,
                        std::string(sim::toString(pending_->lattice)).c_str(), pending_->states,
                        pending_->rule ? " · rule " : "", pending_->rule ? pending_->rule->c_str() : "");
    if (is3D()) {
        ImGui::TextDisabled("placing is 2D for now");
    } else if (const auto why = pendingProblem()) {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.0f), "will not place: %s", why->c_str());
    } else {
        ImGui::TextDisabled("click the grid to place, Esc to cancel");
    }
    if (ImGui::Button("Cancel")) setPending(std::nullopt);
    ImGui::PopID();
}

}  // namespace aether::ui
