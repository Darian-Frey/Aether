// 2D presentation (F-018, SPEC §13).
//
// Draws a state texture through a palette into a viewport rectangle, via
// raylib's batch so it composes with everything else on screen. Reads the
// simulation texture and nothing more: rendering never mutates state
// (ARCHITECTURE §Key invariants 6).

#pragma once

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "render/palette.hpp"
#include "render/view2d.hpp"

#include <optional>
#include <variant>

namespace aether::render {

class Renderer2D {
public:
    static std::variant<Renderer2D, core::Error> create();

    Renderer2D(Renderer2D&&) noexcept;
    Renderer2D& operator=(Renderer2D&&) noexcept;
    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;
    ~Renderer2D();

    void setPalette(const Palette& p);
    const Palette& palette() const { return palette_; }

    void setBackground(Rgba c) { background_ = c; }
    void setAgeShading(bool on) { ageShade_ = on; }
    bool ageShading() const { return ageShade_; }

    // Draws `stateTexture` (a GL_R8UI 2D texture of `spec`'s extents) into
    // `viewport` under `view`. `frameWidth/Height` is the framebuffer being
    // drawn to — the window, or a render texture under BeginTextureMode.
    // Must be called between BeginDrawing and EndDrawing.
    void draw(unsigned int stateTexture, const core::GridSpec& spec, const View2D& view,
              const Rect& viewport, int frameWidth, int frameHeight, unsigned int states);

private:
    Renderer2D() = default;
    void release();

    unsigned int shaderId_ = 0;
    int locState_ = -1, locPalette_ = -1, locFrame_ = -1, locViewport_ = -1, locOrigin_ = -1,
        locZoom_ = -1, locGrid_ = -1, locStates_ = -1, locAge_ = -1, locBackground_ = -1;
    unsigned int paletteTex_ = 0;
    Palette      palette_;
    Rgba         background_{22, 24, 28, 255};
    bool         ageShade_ = false;
};

}  // namespace aether::render
