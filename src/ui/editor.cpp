// The pattern editor (F-029, D-018).
//
// A window of its own rather than a section of the left column: the pad wants
// room to draw in, and floating it keeps the running simulation visible behind
// so a creature can be compared against what it is being built for.
//
// The pad's cells are drawn into ImGui's draw list rather than through
// Renderer2D. That is what keeps `sim::Scratch` free of GL — it has no texture
// and no GPU pair, which is most of why it can be tested headlessly — and a
// pad is small enough for a rectangle per cell to be the right answer, in a
// way the pattern preview is not once patterns get large (IMP-008).

#include "ui/app.hpp"

#include "render/palette.hpp"
#include "rule/library.hpp"
#include "ui/brush.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>

namespace aether::ui {

namespace {

// A label the mouse can rest on for more than the label says. The twin of
// the one in panels.cpp; both are three lines and neither is worth a header.
void editorHint(const char* text) {
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

ImU32 imColour(render::Rgba c) {
    return IM_COL32(c.r, c.g, c.b, 255);
}

}  // namespace

bool App::ensureScratch() {
    if (scratch_) return true;
    if (!sim_) return false;

    // It adopts the live rule, so what the pad shows is what the creature will
    // do where it is going (D-018).
    core::GridSpec spec{sim_->rule().dimensions, static_cast<uint32_t>(editorWidth_),
                        static_cast<uint32_t>(editorHeight_), 1, sim_->rule().cell_type};
    auto made = sim::Scratch::make(spec, sim_->rule());
    if (const auto* e = std::get_if<sim::PatternError>(&made)) {
        log_.error(std::format("pattern editor: {}", e->message));
        return false;
    }
    scratch_.emplace(std::move(std::get<sim::Scratch>(made)));
    log_.info(std::format("pattern editor: {}x{} on {}", spec.width, spec.height,
                          scratch_->ir().metadata.name.value_or(
                              scratch_->ir().metadata.source_notation.value_or("the live rule"))));
    return true;
}

void App::drawEditor() {
    if (!showEditor_) return;
    if (!ensureScratch()) { showEditor_ = false; return; }
    sim::Scratch& pad = *scratch_;

    ImGui::SetNextWindowSize(ImVec2(520, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(panelRect_.x + panelRect_.w + 24.0f, 80.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Pattern editor", &showEditor_)) {
        ImGui::End();
        return;
    }
    ImGui::PushID("editor");

    // --- The pad's own transport. It steps whatever the simulation is doing.
    if (ImGui::Button("Step")) pad.step();
    editorHint("one generation of the pad, on the CPU path. The simulation is not touched");
    ImGui::SameLine();
    ImGui::BeginDisabled(pad.history() == 0);
    if (ImGui::Button("Back")) pad.stepBack();
    ImGui::EndDisabled();
    editorHint(pad.history() == 0 ? "nothing to go back to"
                                  : "back one generation, out of the history ring");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) pad.clear();
    ImGui::SameLine();
    ImGui::TextDisabled("gen %llu  ·  %zu back", static_cast<unsigned long long>(pad.generation()),
                        pad.history());

    // --- The rule. The live one by default, any bundled one instead.
    ImGui::Separator();
    ImGui::TextDisabled("rule: %s", pad.ir().metadata.name.value_or(
                                        pad.ir().metadata.source_notation.value_or("unnamed")).c_str());
    if (sim_ && ImGui::Button("Use the live rule")) {
        if (auto e = pad.setRule(sim_->rule())) log_.error(std::format("pattern editor: {}", e->message));
    }
    editorHint("adopt whatever the simulation is running now");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    if (ImGui::BeginCombo("##padrule", "bundled rule")) {
        for (const auto& entry : library_) {
            // A rule of another dimensionality cannot be adopted by this pad,
            // so it is shown greyed rather than left to fail at the click.
            const bool usable = entry.dimensions == pad.ir().dimensions;
            ImGui::BeginDisabled(!usable);
            if (ImGui::Selectable(entry.name.c_str()) && usable) {
                auto ir = rule::compileLibraryRule(entry, ctx_.boundary);
                if (const auto* bad = std::get_if<std::string>(&ir)) {
                    log_.error(std::format("pattern editor: {}: {}", entry.id, *bad));
                } else if (auto e = pad.setRule(std::get<rule::RuleIR>(ir))) {
                    log_.error(std::format("pattern editor: {}", e->message));
                }
            }
            ImGui::EndDisabled();
            if (!usable && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s is %uD; the pad is %uD", entry.name.c_str(),
                                  entry.dimensions, pad.ir().dimensions);
            }
        }
        ImGui::EndCombo();
    }

    // --- Extent and zoom.
    ImGui::Separator();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("##padw", &editorWidth_, 8, 32);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("##padh", &editorHeight_, 8, 32);
    editorWidth_  = std::clamp(editorWidth_, 1, 256);
    editorHeight_ = std::clamp(editorHeight_, 1, 256);
    ImGui::SameLine();
    if (ImGui::Button("Resize")) {
        if (auto e = pad.resize(static_cast<uint32_t>(editorWidth_), static_cast<uint32_t>(editorHeight_), 1)) {
            log_.error(std::format("pattern editor: {}", e->message));
        }
    }
    editorHint("keeps whatever of the drawing still fits");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 46.0f);
    ImGui::SliderFloat("zoom", &editorZoom_, 3.0f, 32.0f, "%.0f px");

    // --- Out of the pad: into the grid, or into a file.
    ImGui::Separator();
    ImGui::BeginDisabled(!sim_);
    if (ImGui::Button("Place in grid")) {
        // Through F-012's placement path, so it is previewed under the cursor
        // and journalled like any other grid mutation (D-018).
        pending_ = pad.toPattern();
        pending_->name = editorSaveAs_[0] != '\0' ? std::optional<std::string>(editorSaveAs_.data())
                                                  : std::optional<std::string>("scratch pad");
        log_.info("pattern editor: click the grid to place the pad");
    }
    ImGui::EndDisabled();
    editorHint("the whole pad becomes the pending pattern; click the grid to commit it");
    ImGui::SameLine();
    if (ImGui::Button("Take pending")) {
        if (!pending_) {
            log_.info("pattern editor: no pending pattern; open one in the Patterns section first");
        } else if (auto e = pad.place(*pending_, 0, 0, 0)) {
            log_.error(std::format("pattern editor: {}", e->message));
        } else {
            log_.info(std::format("pattern editor: took {}x{} onto the pad",
                                  pending_->width, pending_->height));
            pending_.reset();
        }
    }
    editorHint("copy the pending pattern onto the pad, so anything the Patterns "
               "section can open can be edited here");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
    ImGui::InputTextWithHint("##padname", "name to save as", editorSaveAs_.data(), editorSaveAs_.size());
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        savePatternFile(pad.toPattern(), editorSaveAs_.data(), "scratch");
        patternLibrary_ = sim::loadPatternLibrary({"patterns"});
    }
    editorHint("writes into patterns/, in whichever of the two formats carries it (D-017)");

    ImGui::Separator();
    ImGui::TextDisabled("painting state %u, radius %d · the brush keys work here too",
                        brush_.state, brush_.radius);
    drawEditorGrid();

    ImGui::PopID();
    ImGui::End();
}

void App::drawEditorGrid() {
    sim::Scratch& pad = *scratch_;
    const auto& spec = pad.spec();
    const float cell = editorZoom_;
    const ImVec2 size(static_cast<float>(spec.width) * cell, static_cast<float>(spec.height) * cell);

    // A child region so a pad larger than the window scrolls rather than
    // spilling, and so the painting rectangle is exactly the cells.
    ImGui::BeginChild("padview", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("padcells", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const render::Palette pal = renderer_ ? renderer_->palette()
                                          : render::Palette::defaultFor(pad.ir().states);
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), imColour(pal.entries[0]));
    for (uint32_t y = 0; y < spec.height; ++y) {
        for (uint32_t x = 0; x < spec.width; ++x) {
            const uint8_t s = pad.get(x, y);
            if (s == 0) continue;
            const ImVec2 a(origin.x + static_cast<float>(x) * cell, origin.y + static_cast<float>(y) * cell);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + cell), imColour(pal.entries[s]));
        }
    }
    // Gridlines only where a cell is big enough for them to mean anything.
    if (cell >= 8.0f) {
        const ImU32 line = IM_COL32(255, 255, 255, 24);
        for (uint32_t x = 0; x <= spec.width; ++x) {
            const float px = origin.x + static_cast<float>(x) * cell;
            dl->AddLine(ImVec2(px, origin.y), ImVec2(px, origin.y + size.y), line);
        }
        for (uint32_t y = 0; y <= spec.height; ++y) {
            const float py = origin.y + static_cast<float>(y) * cell;
            dl->AddLine(ImVec2(origin.x, py), ImVec2(origin.x + size.x, py), line);
        }
    }

    if (hovered) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const int cx = static_cast<int>((m.x - origin.x) / cell);
        const int cy = static_cast<int>((m.y - origin.y) / cell);
        // The same brush the canvas paints with (F-011), so the shape a
        // stroke leaves here is the shape it leaves on the grid.
        const bool hex = pad.ir().neighbourhood.type == rule::NeighbourhoodType::Hexagonal;
        const auto spans = brushSpans(cx, cy, brush_.radius, spec.width, spec.height, hex);
        for (const auto& sp : spans) {
            const ImVec2 a(origin.x + static_cast<float>(sp.x0) * cell, origin.y + static_cast<float>(sp.y) * cell);
            dl->AddRectFilled(a, ImVec2(origin.x + static_cast<float>(sp.x1 + 1) * cell, a.y + cell),
                              IM_COL32(255, 255, 255, 40));
        }
        // Left paints the brush state, right erases, which is the one place
        // the editor differs from the canvas: there the right button pans,
        // and there is nothing to pan here.
        const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (left || right) {
            const uint8_t state = right ? 0 : brush_.state;
            for (const auto& sp : spans) {
                for (uint32_t x = sp.x0; x <= sp.x1; ++x) pad.set(x, sp.y, 0, state);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + size.y + 4.0f));
        if (cx >= 0 && cy >= 0 && static_cast<uint32_t>(cx) < spec.width &&
            static_cast<uint32_t>(cy) < spec.height) {
            ImGui::TextDisabled("%d, %d  ·  state %u", cx, cy, pad.get(static_cast<uint32_t>(cx),
                                                                      static_cast<uint32_t>(cy)));
        }
    }
    ImGui::EndChild();
}

}  // namespace aether::ui
