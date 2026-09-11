// Mouse and keyboard over the viewport: pan, zoom, paint.

#include "ui/app.hpp"
#include "ui/brush.hpp"

#include <imgui.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace aether::ui {

void App::paintAt(int cx, int cy) {
    if (!sim_) return;
    const auto& spec = sim_->spec();
    for (const Span& s : brushSpans(cx, cy, brush_.radius, spec.width, spec.height)) {
        sim_->paintSpan(s.x0, s.x1, s.y, 0, brush_.state);
    }
}

void App::updateCanvas(double /*dt*/) {
    if (!sim_) return;
    const ImGuiIO& io = ImGui::GetIO();
    const Vector2 m = GetMousePosition();
    const bool overViewport = m.x >= viewport_.x && m.y >= viewport_.y &&
                              m.x < viewport_.x + viewport_.w && m.y < viewport_.y + viewport_.h;

    // --- Keyboard (when ImGui does not want it) ------------------------------
    if (!io.WantCaptureKeyboard) {
        auto& sch = sim_->scheduler();
        if (IsKeyPressed(KEY_SPACE)) sch.setPaused(!sch.paused());
        if (IsKeyPressed(KEY_N)) sch.requestSingleStep();
        if (IsKeyPressed(KEY_F)) fitView();
        if (IsKeyPressed(KEY_R)) sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
        if (IsKeyPressed(KEY_C)) sim_->clear();
        if (IsKeyPressed(KEY_LEFT_BRACKET))  brush_.radius = std::max(0, brush_.radius - 1);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) brush_.radius = std::min(64, brush_.radius + 1);
        for (int k = 0; k <= 9; ++k) {
            if (IsKeyPressed(KEY_ZERO + k) && k < sim_->rule().states) brush_.state = static_cast<uint8_t>(k);
        }
    }

    if (io.WantCaptureMouse && !panning_ && !lastPaintCell_) return;

    // --- Zoom about the cursor ----------------------------------------------
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && overViewport) {
        view_.zoomAt(m.x, m.y, wheel > 0 ? 1.25 : 0.8, viewport_);
    }

    // --- Pan with right or middle button -----------------------------------
    const bool panButton = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    if (panButton && (panning_ || overViewport)) {
        const Vector2 d = GetMouseDelta();
        view_.pan(d.x, d.y);
        panning_ = true;
    } else {
        panning_ = false;
    }

    // --- Paint with the left button -----------------------------------------
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (lastPaintCell_ || overViewport)) {
        const auto [cx, cy] = view_.screenToCell(m.x, m.y, viewport_);
        const int ix = static_cast<int>(std::floor(cx));
        const int iy = static_cast<int>(std::floor(cy));
        if (lastPaintCell_) {
            for (const auto& [px, py] : strokePoints(lastPaintCell_->first, lastPaintCell_->second, ix, iy,
                                                     std::max(1, brush_.radius))) {
                paintAt(px, py);
            }
        } else {
            paintAt(ix, iy);
        }
        lastPaintCell_ = std::pair{ix, iy};
    } else {
        lastPaintCell_.reset();
    }
}

}  // namespace aether::ui
