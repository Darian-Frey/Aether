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
#include <utility>
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
    const Palette& palette() const { return cfg_.palette; }

    void setBackground(Rgba c) { cfg_.background = c; }
    void setAgeShading(bool on) { cfg_.ageShade = on; }
    bool ageShading() const { return cfg_.ageShade; }

    // First state of the rule's ageing tail, or nullopt (SPEC §7 decay).
    void setDecayFrom(std::optional<uint16_t> from) { cfg_.decayFrom = from; }

    // Draws `stateTexture` — a 2D texture of `spec`'s extents, GL_R8UI for a
    // u8 grid and GL_R32F for a float one — into `viewport` under `view`.
    // `frameWidth/Height` is the framebuffer being drawn to: the window, or a
    // render texture under BeginTextureMode. Must be called between
    // BeginDrawing and EndDrawing.
    void draw(unsigned int stateTexture, const core::GridSpec& spec, const View2D& view,
              const Rect& viewport, int frameWidth, int frameHeight, unsigned int states);

    // --- Overlay: a pattern drawn over the grid, not into it (IMP-008) ------
    //
    // The same palette pass over a pattern's own cells, so a preview is drawn
    // in the colours it will actually become rather than as a wash of
    // rectangles. Takes the texture rather than owning one, exactly as `draw`
    // does: the caller already has a `core::GpuGrid` to make a state texture
    // of either cell type, and a second way of making one here would be a
    // second thing to keep in step, besides a GL handle in a class whose move
    // protection was hard enough to get right once (IMP-007, BUG-007).
    //
    // `cellX`/`cellY` place the pattern's cell (0, 0) at that grid cell, under
    // the same view the grid is drawn with. `tint` is mixed in by its own
    // alpha: a light wash for a pattern that will place, a red one for a
    // pattern that will not. Cells the pattern leaves empty are not drawn, so
    // what is underneath still shows. Nothing here touches simulation state
    // (invariant 6).
    void drawOverlay(unsigned int stateTexture, const core::GridSpec& spec, const View2D& view,
                     const Rect& viewport, int frameWidth, int frameHeight, unsigned int states,
                     double cellX, double cellY, Rgba tint);

    // --- A band of rows, for the space-time view (F-005) --------------------
    //
    // `viewport` shows the texture's rows from `firstRow` down, at `zoom`
    // pixels per cell, with column 0 at its left edge. The space-time history
    // is a ring, so its seam is drawn as two bands rather than by shuffling
    // the texture's rows every generation; this is the entry point that lets
    // a caller say which row a band starts at, which `draw` cannot because it
    // takes a camera rather than an origin.
    void drawBand(unsigned int stateTexture, const core::GridSpec& spec, const Rect& viewport,
                  int frameWidth, int frameHeight, unsigned int states,
                  double zoom, double firstRow);

private:
    Renderer2D() = default;
    void release();

    // Both public draws are this one pass with different uniforms, so the
    // grid and its overlay cannot drift in how they map a pixel to a cell.
    // `originShift` moves the pattern's cell (0, 0) onto a grid cell; it is
    // zero for the grid itself.
    void drawPass(unsigned int stateTexture, const core::GridSpec& spec, const View2D& view,
                  const Rect& viewport, int frameWidth, int frameHeight, unsigned int states,
                  bool overlay, std::pair<double, double> originShift, Rgba tint);

    // One compiled variant of the palette pass. The locations belong with the
    // program: they are meaningless without it, and exchanging them together
    // on a move is what stops one being left behind (IMP-007, BUG-007).
    struct Program {
        unsigned int id = 0;
        int state = -1, palette = -1, frame = -1, viewport = -1, origin = -1, zoom = -1,
            grid = -1, states = -1, age = -1, background = -1, lattice = -1, decayFrom = -1,
            overlay = -1, tint = -1;
    };

    // GL handles: exchanged wholesale on move, never listed one at a time.
    struct Owned {
        Program      discrete;     // usampler2D, state indices
        Program      continuous;   // sampler2D, values in [0, 1]
        unsigned int paletteTex = 0;
    };
    // Plain state: copied wholesale.
    struct Config {
        Palette      palette;
        Rgba         background{22, 24, 28, 255};
        bool         ageShade = false;
        std::optional<uint16_t> decayFrom;
    };

    Owned  owned_;
    Config cfg_;
};

}  // namespace aether::render
