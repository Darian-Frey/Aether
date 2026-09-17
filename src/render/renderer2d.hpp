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

private:
    Renderer2D() = default;
    void release();

    // One compiled variant of the palette pass. The locations belong with the
    // program: they are meaningless without it, and exchanging them together
    // on a move is what stops one being left behind (IMP-007, BUG-007).
    struct Program {
        unsigned int id = 0;
        int state = -1, palette = -1, frame = -1, viewport = -1, origin = -1, zoom = -1,
            grid = -1, states = -1, age = -1, background = -1, lattice = -1, decayFrom = -1;
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
