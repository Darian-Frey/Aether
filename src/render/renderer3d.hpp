// 3D presentation (F-019, SPEC §13): volume raymarch through the 3D state
// texture. Reads only. Requires a GL 4.3 context.

#pragma once

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "render/orbit.hpp"
#include "render/palette.hpp"
#include "render/view2d.hpp"   // Rect

#include <array>
#include <variant>

namespace aether::render {

struct VolumeSettings {
    std::array<double, 3> clipMin{0, 0, 0};   // cells, inclusive; fractions of the grid are the UI's business
    std::array<double, 3> clipMax{0, 0, 0};   // cells, exclusive; 0 = grid extent
    double opacity = 1.0;                      // multiplies palette alpha
};

class Renderer3D {
public:
    static std::variant<Renderer3D, core::Error> create();

    Renderer3D(Renderer3D&&) noexcept;
    Renderer3D& operator=(Renderer3D&&) noexcept;
    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;
    ~Renderer3D();

    void setPalette(const Palette& p);
    const Palette& palette() const { return cfg_.palette; }
    void setBackground(Rgba c) { cfg_.background = c; }

    // Draws `stateTexture` (GL_TEXTURE_3D, GL_R8UI, `spec`'s extents) into
    // `viewport` as seen from `camera`. Between BeginDrawing and EndDrawing.
    void draw(unsigned int stateTexture, const core::GridSpec& spec, const Orbit& camera,
              const VolumeSettings& settings, const Rect& viewport, int frameWidth, int frameHeight);

private:
    Renderer3D() = default;
    void release();

    struct Owned {
        unsigned int shader = 0;
        unsigned int paletteTex = 0;
    };
    struct Config {
        int locState = -1, locPalette = -1, locFrame = -1, locViewport = -1, locCamPos = -1, locForward = -1,
            locRight = -1, locUp = -1, locTanHalf = -1, locGrid = -1, locClipMin = -1, locClipMax = -1,
            locOpacity = -1, locMaxSteps = -1, locBackground = -1;
        Palette palette;
        Rgba    background{22, 24, 28, 255};
    };
    Owned  owned_;
    Config cfg_;
};

}  // namespace aether::render
