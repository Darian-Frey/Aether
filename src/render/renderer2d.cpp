#include "render/renderer2d.hpp"

#include "render/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <utility>

namespace aether::render {

std::variant<Renderer2D, core::Error> Renderer2D::create() {
    Renderer2D r;
    const Shader sh = LoadShaderFromMemory(shaders::kPalette2dVert, shaders::kPalette2dFrag);
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault()) {
        return core::Error{"palette2d shader failed to compile (see raylib log)"};
    }
    r.shaderId_      = sh.id;
    r.locState_      = GetShaderLocation(sh, "stateTex");
    r.locPalette_    = GetShaderLocation(sh, "paletteTex");
    r.locFrame_      = GetShaderLocation(sh, "frameSize");
    r.locViewport_   = GetShaderLocation(sh, "viewport");
    r.locOrigin_     = GetShaderLocation(sh, "origin");
    r.locZoom_       = GetShaderLocation(sh, "zoom");
    r.locGrid_       = GetShaderLocation(sh, "gridSize");
    r.locStates_     = GetShaderLocation(sh, "states");
    r.locAge_        = GetShaderLocation(sh, "ageShade");
    r.locBackground_ = GetShaderLocation(sh, "background");
    // raylib allocated locs[]; we keep only the id and free its table.
    RL_FREE(sh.locs);

    r.paletteTex_ = rlLoadTexture(nullptr, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    if (r.paletteTex_ == 0) {
        rlUnloadShaderProgram(r.shaderId_);
        return core::Error{"palette texture allocation failed"};
    }
    rlTextureParameters(r.paletteTex_, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(r.paletteTex_, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    r.setPalette(Palette::defaultFor(2));
    return r;
}

Renderer2D::Renderer2D(Renderer2D&& o) noexcept
    : shaderId_(o.shaderId_), locState_(o.locState_), locPalette_(o.locPalette_), locFrame_(o.locFrame_),
      locViewport_(o.locViewport_), locOrigin_(o.locOrigin_), locZoom_(o.locZoom_), locGrid_(o.locGrid_),
      locStates_(o.locStates_), locAge_(o.locAge_), locBackground_(o.locBackground_),
      paletteTex_(o.paletteTex_), palette_(o.palette_), background_(o.background_), ageShade_(o.ageShade_) {
    o.shaderId_ = 0;
    o.paletteTex_ = 0;
}

Renderer2D& Renderer2D::operator=(Renderer2D&& o) noexcept {
    if (this != &o) {
        release();
        new (this) Renderer2D(std::move(o));
    }
    return *this;
}

Renderer2D::~Renderer2D() {
    release();
}

void Renderer2D::release() {
    if (shaderId_ != 0) rlUnloadShaderProgram(shaderId_);
    if (paletteTex_ != 0) rlUnloadTexture(paletteTex_);
    shaderId_ = 0;
    paletteTex_ = 0;
}

void Renderer2D::setPalette(const Palette& p) {
    palette_ = p;
    rlUpdateTexture(paletteTex_, 0, 0, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, palette_.entries.data());
}

void Renderer2D::draw(unsigned int stateTexture, const core::GridSpec& spec, const View2D& view,
                      const Rect& vp, int frameWidth, int frameHeight, unsigned int states) {
    const auto [ox, oy] = view.snappedOrigin(vp);
    const float frame[2]    = {static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
    const float viewport[4] = {vp.x, vp.y, vp.w, vp.h};
    const float origin[2]   = {static_cast<float>(ox), static_cast<float>(oy)};
    const float zoom        = static_cast<float>(view.zoom);
    const float grid[2]     = {static_cast<float>(spec.width), static_cast<float>(spec.height)};
    const int   nStates     = static_cast<int>(states);
    const int   age         = ageShade_ ? 1 : 0;
    const float bg[4]       = {background_.r / 255.0f, background_.g / 255.0f, background_.b / 255.0f, background_.a / 255.0f};

    // rlSetShader (inside BeginShaderMode) flushes the batch, and a flush
    // clears the registered sampler textures, so the shader switch must
    // come before the samplers are set, not after.
    Shader sh{};
    sh.id = shaderId_;
    int locs[RL_MAX_SHADER_LOCATIONS];
    for (int& l : locs) l = -1;
    locs[SHADER_LOC_MATRIX_MVP]        = rlGetLocationUniform(shaderId_, "mvp");
    locs[SHADER_LOC_VERTEX_POSITION]   = rlGetLocationAttrib(shaderId_, "vertexPosition");
    locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(shaderId_, "vertexTexCoord");
    locs[SHADER_LOC_VERTEX_COLOR]      = rlGetLocationAttrib(shaderId_, "vertexColor");
    sh.locs = locs;

    rlDrawRenderBatchActive();
    BeginShaderMode(sh);
    rlEnableShader(shaderId_);
    rlSetUniform(locFrame_, frame, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(locViewport_, viewport, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(locOrigin_, origin, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(locZoom_, &zoom, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(locGrid_, grid, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(locStates_, &nStates, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locAge_, &age, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(locBackground_, bg, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniformSampler(locState_, stateTexture);
    rlSetUniformSampler(locPalette_, paletteTex_);

    // A quad over the viewport; the fragment shader does the rest. The
    // batch flushes inside EndShaderMode, while `locs` is still alive.
    DrawRectangle(static_cast<int>(vp.x), static_cast<int>(vp.y), static_cast<int>(vp.w), static_cast<int>(vp.h), WHITE);
    EndShaderMode();
    rlDrawRenderBatchActive();
}

}  // namespace aether::render
