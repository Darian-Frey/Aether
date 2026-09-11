// Dear ImGui panels.

#include "ui/app.hpp"

#include "rule/lut.hpp"

#include <imgui.h>
#include <raylib.h>

#include <cmath>
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
    ImGui::PopID();
}

void App::drawGridPanel() {
    if (!sim_) return;
    ImGui::PushID("grid");
    ImGui::InputInt("Width", &newWidth_, 16, 256);
    ImGui::InputInt("Height", &newHeight_, 16, 256);
    newWidth_ = std::clamp(newWidth_, 1, 16384);
    newHeight_ = std::clamp(newHeight_, 1, 16384);
    if (ImGui::Button("New grid")) {
        const rule::RuleIR ir = sim_->rule();
        const sim::Path path = sim_->path();
        sim_.reset();
        if (createSimulation(static_cast<uint32_t>(newWidth_), static_cast<uint32_t>(newHeight_), ir, path)) {
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
    if (ImGui::Button("Reset palette")) { pal = render::Palette::defaultFor(sim_->rule().states); changed = true; }
    if (changed) renderer_->setPalette(pal);
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
