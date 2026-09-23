// Mouse and keyboard over the viewport: pan, zoom, paint.

#include "ui/app.hpp"
#include "ui/brush.hpp"

#include <imgui.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <tuple>

namespace aether::ui {

void App::paintAt(int cx, int cy) {
    if (!sim_) return;
    const auto& spec = sim_->spec();
    const bool hex = sim_->rule().neighbourhood.type == rule::NeighbourhoodType::Hexagonal;
    for (const Span& s : brushSpans(cx, cy, brush_.radius, spec.width, spec.height, hex)) {
        sim_->paintSpan(s.x0, s.x1, s.y, 0, brush_.state);
    }
}

// Paints the brush on the current slice through `cell` (F-011, 3D). The
// disc lives in the slice's two in-plane axes p < q; spans run along p.
void App::paintAt3D(const std::array<int, 3>& cell) {
    if (!sim_) return;
    const auto& spec = sim_->spec();
    const uint32_t ext[3] = {spec.width, spec.height, spec.depth};
    const size_t ax = static_cast<size_t>(sliceAxis_);
    const size_t p = ax == 0 ? 1 : 0;
    const size_t q = ax == 2 ? 1 : 2;
    for (const Span& sp : brushSpans(cell[p], cell[q], brush_.radius, ext[p], ext[q])) {
        uint32_t c[3] = {0, 0, 0};
        c[ax] = static_cast<uint32_t>(cell[ax]);
        c[q] = sp.y;
        if (p == 0) {
            sim_->paintSpan(sp.x0, sp.x1, c[1], c[2], brush_.state);
        } else {
            // x is the slice axis: the span runs along y at fixed x and z.
            for (uint32_t y = sp.x0; y <= sp.x1; ++y) sim_->paintSpan(c[0], c[0], y, c[2], brush_.state);
        }
    }
}

// The cell the cursor is over, unclipped, or nothing when it is not over the
// viewport. Shared by selection and by the pattern preview, so the two cannot
// disagree about where the mouse is.
std::optional<std::pair<int, int>> App::cellUnderCursor() const {
    if (!sim_ || is3D()) return std::nullopt;
    const Vector2 m = GetMousePosition();
    if (m.x < viewport_.x || m.y < viewport_.y ||
        m.x >= viewport_.x + viewport_.w || m.y >= viewport_.y + viewport_.h) {
        return std::nullopt;
    }
    const auto [u, v] = view_.screenToCell(m.x, m.y, viewport_);
    if (view_.lattice == render::Lattice::Hex) {
        const auto [q, r] = view_.fromCellSpace(u - 0.5, v - 0.5);
        const auto [hx, hy] = render::View2D::hexRound(q, r);
        return std::pair{hx, hy};
    }
    return std::pair{static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v))};
}

void App::updateCanvas(double /*dt*/) {
    if (!sim_) return;
    const ImGuiIO& io = ImGui::GetIO();
    const Vector2 m = GetMousePosition();
    const bool overViewport = m.x >= viewport_.x && m.y >= viewport_.y &&
                              m.x < viewport_.x + viewport_.w && m.y < viewport_.y + viewport_.h;

    // --- Shortcuts that do not depend on how many dimensions there are -------
    //
    // These used to be written out once per branch below, thirteen of them
    // duplicated by copy, and twice that has gone wrong: F1 was added to the
    // 3D branch only (BUG-018), and the `swallowLeft_` clear and the Esc
    // cancel were hoisted into it rather than above it, which left painting
    // dead in 2D after any pattern was placed (BUG-019). A key that belongs
    // to both now has one home.
    if (!io.WantCaptureKeyboard) {
        auto& sch = sim_->scheduler();
        if (IsKeyPressed(KEY_SPACE)) sch.setPaused(!sch.paused());
        if (IsKeyPressed(KEY_N)) sch.requestSingleStep();
        if (IsKeyPressed(KEY_F)) fitView();
        if (IsKeyPressed(KEY_E)) { showEditor_ = !showEditor_; if (showEditor_) ensureScratch(); }
        if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_SLASH)) showHelp_ = !showHelp_;
        if (IsKeyPressed(KEY_R)) sim_->fillRandom(std::vector<double>(density_.begin(), density_.end()));
        if (IsKeyPressed(KEY_C)) sim_->clear();
        if (IsKeyPressed(KEY_LEFT_BRACKET))  brush_.radius = std::max(0, brush_.radius - 1);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) brush_.radius = std::min(64, brush_.radius + 1);
        for (int k = 0; k <= 9; ++k) {
            if (IsKeyPressed(KEY_ZERO + k) && k < sim_->rule().states) brush_.state = static_cast<uint8_t>(k);
        }
        // Above both mouse-capture returns, so a pattern can be cancelled with
        // the cursor anywhere, including over the panel that opened it.
        if (pending_ && IsKeyPressed(KEY_ESCAPE)) {
            setPending(std::nullopt);
            log_.info("pattern cancelled");
        }
    }
    // Likewise above both returns, or a button released over a panel leaves
    // the flag set and swallows the next click on the canvas.
    if (swallowLeft_ && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) swallowLeft_ = false;

    if (is3D()) {
        // The slice controls are the only keys that mean anything solely here.
        if (!io.WantCaptureKeyboard) {
            if (IsKeyPressed(KEY_S)) sliceMode_ = !sliceMode_;
            const int ext = static_cast<int>(sliceAxis_ == 0 ? sim_->spec().width : sliceAxis_ == 1 ? sim_->spec().height : sim_->spec().depth);
            if (IsKeyPressed(KEY_COMMA))  sliceIndex_ = std::max(0, sliceIndex_ - 1);
            if (IsKeyPressed(KEY_PERIOD)) sliceIndex_ = std::min(ext - 1, sliceIndex_ + 1);
        }

        if (io.WantCaptureMouse && !panning_ && !lastPaintCell_) return;
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && overViewport) orbit_.zoom(wheel > 0 ? 0.85 : 1.18);
        const bool orbitButton = IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
        if (orbitButton && (panning_ || overViewport)) {
            const Vector2 d = GetMouseDelta();
            orbit_.rotate(-d.x * 0.008, d.y * 0.008);
            panning_ = true;
        } else {
            panning_ = false;
        }
        if (sliceMode_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (lastPaintCell_ || overViewport)) {
            const auto& sp = sim_->spec();
            const auto hit = orbit_.pickOnSlab(m.x, m.y, viewport_, sliceAxis_, sliceIndex_, sp.width, sp.height, sp.depth);
            if (hit) paintAt3D(*hit);
            lastPaintCell_ = std::pair{0, 0};   // marks a stroke in progress
        } else {
            lastPaintCell_.reset();
        }
        return;
    }

    if (io.WantCaptureMouse && !panning_ && !lastPaintCell_) return;

    // A 1D run is shown as its history, not as its row, so the mouse means
    // something different: the wheel sets how many pixels a generation gets,
    // and there is nothing to paint or pan. Painting would write into a row
    // the diagram has already scrolled past (F-005).
    if (is1D()) {
        const float wheel1d = GetMouseWheelMove();
        if (wheel1d != 0.0f && overViewport) {
            const int before = spaceTimeZoom_;
            spaceTimeZoom_ = std::clamp(spaceTimeZoom_ + (wheel1d > 0 ? 1 : -1), 1, 16);
            if (spaceTimeZoom_ != before) rebuildSpaceTime();
        }
        return;
    }

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

    // --- Shift-drag selects a region ----------------------------------------
    // Before the pending-pattern branch, so a selection can be made while one
    // is loaded; a modifier rather than a mode, so there is nothing to leave
    // switched on by accident.
    if (!is3D()) {
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (shift && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && overViewport) {
            selectAnchor_ = cellUnderCursor();
            selection_.reset();
        }
        if (selectAnchor_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            if (const auto now = cellUnderCursor()) {
                const auto [ax, ay] = *selectAnchor_;
                const auto [bx, by] = *now;
                const auto clampX = [&](int v) {
                    return static_cast<uint32_t>(std::clamp(v, 0, static_cast<int>(sim_->spec().width) - 1));
                };
                const auto clampY = [&](int v) {
                    return static_cast<uint32_t>(std::clamp(v, 0, static_cast<int>(sim_->spec().height) - 1));
                };
                selection_ = Selection{clampX(std::min(ax, bx)), clampY(std::min(ay, by)),
                                       clampX(std::max(ax, bx)), clampY(std::max(ay, by))};
            }
            return;
        }
        if (selectAnchor_ && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) selectAnchor_.reset();
    }

    // --- A pending pattern takes the left button -----------------------------
    // Placing and painting cannot share it: a click meant to put a pattern
    // down would otherwise also daub the brush under it.
    if (pending_) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && overViewport) {
            if (const auto origin = pendingOrigin()) {
                const auto [ox, oy] = *origin;
                if (ox < 0 || oy < 0) {
                    log_.error("the pattern would hang over the edge of the grid");
                } else if (auto e = sim_->placePattern(*pending_, static_cast<uint32_t>(ox),
                                                       static_cast<uint32_t>(oy), 0)) {
                    log_.error(e->message);
                } else {
                    log_.info(std::format("placed {} at ({}, {})",
                                          pending_->name.value_or("pattern"), ox, oy));
                    setPending(std::nullopt);
                    // The button is still down for the frames after this one,
                    // and with nothing pending the paint branch below would
                    // take them: one click would place *and* daub.
                    swallowLeft_ = true;
                }
            }
        }
        return;
    }

    // --- Paint with the left button -----------------------------------------
    if (swallowLeft_) return;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (lastPaintCell_ || overViewport)) {
        // cellAt clips to the grid; for strokes that leave it we still want
        // a coordinate, so derive one the same way without the clip.
        const auto [u, v] = view_.screenToCell(m.x, m.y, viewport_);
        int ix, iy;
        if (view_.lattice == render::Lattice::Hex) {
            const auto [q, r] = view_.fromCellSpace(u - 0.5, v - 0.5);
            std::tie(ix, iy) = render::View2D::hexRound(q, r);
        } else {
            ix = static_cast<int>(std::floor(u));
            iy = static_cast<int>(std::floor(v));
        }
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
