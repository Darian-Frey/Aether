// Dear ImGui panels.

#include "ui/app.hpp"

#include "rule/lut.hpp"
#include "sim/session.hpp"

#include <imgui.h>
#include <raylib.h>

#include <cmath>
#include <cstring>
#include <format>

namespace aether::ui {

namespace {

constexpr float kPanelWidth = 360.0f;

}  // namespace

void App::drawPanels() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(GetScreenHeight())), ImGuiCond_Always);
    ImGui::Begin("Aether", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar);

    if (sim_) {
        const auto& st = sim_->scheduler().stats();
        ImGui::Text("gen %llu", static_cast<unsigned long long>(sim_->generation()));
        ImGui::SameLine(0, 16);
        ImGui::Text("%.0f gen/s", st.achieved_gps);
        ImGui::SameLine(0, 16);
        ImGui::Text("%d fps", GetFPS());
        if (st.below_target) {
            ImGui::SameLine(0, 16);
            ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "below target");
        }
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Rule", ImGuiTreeNodeFlags_DefaultOpen)) drawRulePanel();
    if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) drawSimulationPanel();
    if (ImGui::CollapsingHeader("Grid")) drawGridPanel();
    if (is3D() && ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) drawViewPanel();
    if (ImGui::CollapsingHeader("Mutation", ImGuiTreeNodeFlags_DefaultOpen)) drawMutationPanel();
    if (ImGui::CollapsingHeader("Lineage", ImGuiTreeNodeFlags_DefaultOpen)) drawLineagePanel();
    if (ImGui::CollapsingHeader("Session")) drawSessionPanel();
    if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) drawBrushPanel();
    if (ImGui::CollapsingHeader("Palette")) drawPalettePanel();
    if (ImGui::CollapsingHeader("Log")) drawLogPanel();

    ImGui::End();
}

void App::drawRulePanel() {
    ImGui::PushID("rule");
    ImGui::InputTextMultiline("##src", ruleText_.data(), ruleText_.size(), ImVec2(-1, 96),
                              ImGuiInputTextFlags_AllowTabInput);
    bool apply = ImGui::Button("Compile");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+Enter");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("B3/S23, B2/S/C3, or a table block");
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
        ImGui::TextWrapped("%s", ruleSummary_.c_str());
    }
    ImGui::PopID();
}

void App::drawSimulationPanel() {
    if (!sim_) return;
    ImGui::PushID("sim");
    auto& sch = sim_->scheduler();

    if (ImGui::SliderFloat("Target gen/s", &targetGpsLog_, -1.0f, 4.0f,
                           std::format("{:.3g}", std::pow(10.0, targetGpsLog_)).c_str())) {
        sch.setTargetRate(std::pow(10.0, targetGpsLog_));
    }

    if (ImGui::Button(sch.paused() ? "Resume" : "Pause")) sch.setPaused(!sch.paused());
    ImGui::SameLine();
    if (ImGui::Button("Step")) sch.requestSingleStep();
    ImGui::SameLine();
    if (ImGui::Button("Burst")) sch.requestBurst(static_cast<uint64_t>(std::max(1, burstCount_)));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputInt("##burst", &burstCount_, 0);
    if (sch.burstRemaining() > 0) {
        ImGui::SameLine();
        ImGui::Text("%llu left", static_cast<unsigned long long>(sch.burstRemaining()));
        ImGui::SameLine();
        if (ImGui::SmallButton("cancel")) sch.cancelBurst();
    }

    int path = sim_->path() == sim::Path::Gpu ? 0 : 1;
    if (ImGui::RadioButton("GPU", path == 0)) { path = 0; }
    ImGui::SameLine();
    if (ImGui::RadioButton("CPU reference", path == 1)) { path = 1; }
    const sim::Path want = path == 0 ? sim::Path::Gpu : sim::Path::Cpu;
    if (want != sim_->path()) {
        if (auto e = sim_->setPath(want)) log_.error(e->message);
        else log_.info(want == sim::Path::Gpu ? "switched to GPU path" : "switched to CPU reference path");
    }

    int cap = static_cast<int>(sch.maxStepsPerFrame());
    if (ImGui::SliderInt("Max steps/frame", &cap, 1, 1024, "%d", ImGuiSliderFlags_Logarithmic)) {
        sch.setMaxStepsPerFrame(static_cast<uint32_t>(cap));
    }
    if (sch.stats().effective_cap < sch.maxStepsPerFrame()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(now %u)", sch.stats().effective_cap);
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
    ImGui::TextUnformatted("Random fill density");
    for (size_t i = 0; i < density_.size() && i < 16; ++i) {
        ImGui::SliderFloat(std::format("state {}", i + 1).c_str(), &density_[i], 0.0f, 1.0f);
    }
    if (ImGui::Button("Fill (R)")) sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
    ImGui::SameLine();
    if (ImGui::Button("Clear (C)")) sim_->clear();
    ImGui::SameLine();
    if (ImGui::Button("Fit view (F)")) fitView();
    ImGui::PopID();
}

void App::drawMutationPanel() {
    if (!sim_) return;
    ImGui::PushID("mutation");
    bool changed = ImGui::Checkbox("Cell mutation", &cellMutationOn_);
    ImGui::SameLine();
    ImGui::TextDisabled("seed B %llu", static_cast<unsigned long long>(sim_->seedB()));
    ImGui::BeginDisabled(!cellMutationOn_);
    changed |= ImGui::SliderFloat("p per cell", &cellMutationLog_, -7.0f, 0.0f,
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
    rchanged |= ImGui::SliderInt("every N gens", &ruleInterval_, 1, 5000, "%d", ImGuiSliderFlags_Logarithmic);
    rchanged |= ImGui::SliderInt("edits per event", &ruleMagnitude_, 1, 32);
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
    ImGui::Checkbox("Slice mode (S)", &sliceMode_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show one plane; left-drag paints on it");
    ImGui::BeginDisabled(!sliceMode_);
    ImGui::Combo("Axis", &sliceAxis_, "x\0y\0z\0");
    const int ext = static_cast<int>(sliceAxis_ == 0 ? sp.width : sliceAxis_ == 1 ? sp.height : sp.depth);
    sliceIndex_ = std::clamp(sliceIndex_, 0, ext - 1);
    ImGui::SliderInt("Index (, .)", &sliceIndex_, 0, ext - 1);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Right drag orbits, wheel zooms, F fits.");
    if (ImGui::SmallButton("Fit (F)")) fitView();
    ImGui::PopID();
}

void App::drawBrushPanel() {
    if (!sim_) return;
    ImGui::PushID("brush");
    int state = brush_.state;
    if (ImGui::SliderInt("State (0-9)", &state, 0, sim_->rule().states - 1)) brush_.state = static_cast<uint8_t>(state);
    ImGui::SliderInt("Radius ([ ])", &brush_.radius, 0, 64);
    ImGui::TextDisabled("Left drag paints, right drag pans, wheel zooms.");
    ImGui::TextDisabled("Space pause, N step.");
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
        pal = render::Palette::defaultFor(sim_->rule().states, sim_->rule().metadata.decay_from);
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

}  // namespace aether::ui
