#include "render/renderer2d.hpp"

#include "render/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <format>
#include <optional>
#include <string>
#include <utility>

namespace aether::render {

namespace {

// The fragment source is compiled twice; the version line and the variant
// define are prepended here rather than living in the file, so one source
// serves both (SPEC §13).
std::string fragmentSource(bool f32) {
    std::string src = "#version 430\n";
    if (f32) src += "#define AETHER_F32 1\n";
    src += shaders::kPalette2dFrag;
    return src;
}

}  // namespace

std::variant<Renderer2D, core::Error> Renderer2D::create() {
    Renderer2D r;
    auto compile = [](bool f32, Renderer2D::Program& out) -> std::optional<core::Error> {
        const std::string frag = fragmentSource(f32);
        const Shader sh = LoadShaderFromMemory(shaders::kPalette2dVert, frag.c_str());
        if (sh.id == 0 || sh.id == rlGetShaderIdDefault()) {
            return core::Error{std::format("palette2d{} shader failed to compile (see raylib log)",
                                           f32 ? " (f32)" : "")};
        }
        out.id         = sh.id;
        out.state      = GetShaderLocation(sh, "stateTex");
        out.palette    = GetShaderLocation(sh, "paletteTex");
        out.frame      = GetShaderLocation(sh, "frameSize");
        out.viewport   = GetShaderLocation(sh, "viewport");
        out.origin     = GetShaderLocation(sh, "origin");
        out.zoom       = GetShaderLocation(sh, "zoom");
        out.grid       = GetShaderLocation(sh, "gridSize");
        out.states     = GetShaderLocation(sh, "states");
        out.age        = GetShaderLocation(sh, "ageShade");
        out.background = GetShaderLocation(sh, "background");
        out.lattice    = GetShaderLocation(sh, "lattice");
        out.decayFrom  = GetShaderLocation(sh, "decayFrom");
        // raylib allocated locs[]; we keep only the id and free its table.
        RL_FREE(sh.locs);
        return std::nullopt;
    };
    if (auto e = compile(false, r.owned_.discrete)) return *e;
    if (auto e = compile(true, r.owned_.continuous)) {
        rlUnloadShaderProgram(r.owned_.discrete.id);
        return *e;
    }

    r.owned_.paletteTex = rlLoadTexture(nullptr, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    if (r.owned_.paletteTex == 0) {
        rlUnloadShaderProgram(r.owned_.discrete.id);
        rlUnloadShaderProgram(r.owned_.continuous.id);
        return core::Error{"palette texture allocation failed"};
    }
    rlTextureParameters(r.owned_.paletteTex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(r.owned_.paletteTex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    r.setPalette(Palette::defaultFor(2));
    return r;
}

// Handles move, plain state copies. Nothing is listed member by member, which
// is the whole point: a field added later cannot be dropped here (IMP-007).
Renderer2D::Renderer2D(Renderer2D&& o) noexcept
    : owned_(std::exchange(o.owned_, Owned{})), cfg_(o.cfg_) {}

Renderer2D& Renderer2D::operator=(Renderer2D&& o) noexcept {
    if (this != &o) {
        release();
        owned_ = std::exchange(o.owned_, Owned{});
        cfg_ = o.cfg_;
    }
    return *this;
}

Renderer2D::~Renderer2D() {
    release();
}

void Renderer2D::release() {
    if (owned_.discrete.id != 0) rlUnloadShaderProgram(owned_.discrete.id);
    if (owned_.continuous.id != 0) rlUnloadShaderProgram(owned_.continuous.id);
    if (owned_.paletteTex != 0) rlUnloadTexture(owned_.paletteTex);
    owned_ = Owned{};
}

void Renderer2D::setPalette(const Palette& p) {
    cfg_.palette = p;
    rlUpdateTexture(owned_.paletteTex, 0, 0, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                    cfg_.palette.entries.data());
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
    const int   age         = cfg_.ageShade ? 1 : 0;
    const int   lattice     = view.lattice == Lattice::Hex ? 1 : 0;
    const int   decayFrom   = cfg_.decayFrom ? static_cast<int>(*cfg_.decayFrom) : -1;
    const float bg[4]       = {cfg_.background.r / 255.0f, cfg_.background.g / 255.0f,
                               cfg_.background.b / 255.0f, cfg_.background.a / 255.0f};

    // A float grid is a different sampler type, so it is a different program.
    const Program& prog = spec.cell_type == core::CellType::F32 ? owned_.continuous : owned_.discrete;

    // rlSetShader (inside BeginShaderMode) flushes the batch, and a flush
    // clears the registered sampler textures, so the shader switch must
    // come before the samplers are set, not after.
    Shader sh{};
    sh.id = prog.id;
    int locs[RL_MAX_SHADER_LOCATIONS];
    for (int& l : locs) l = -1;
    locs[SHADER_LOC_MATRIX_MVP]        = rlGetLocationUniform(prog.id, "mvp");
    locs[SHADER_LOC_VERTEX_POSITION]   = rlGetLocationAttrib(prog.id, "vertexPosition");
    locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(prog.id, "vertexTexCoord");
    locs[SHADER_LOC_VERTEX_COLOR]      = rlGetLocationAttrib(prog.id, "vertexColor");
    sh.locs = locs;

    rlDrawRenderBatchActive();
    BeginShaderMode(sh);
    rlEnableShader(prog.id);
    rlSetUniform(prog.frame, frame, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(prog.viewport, viewport, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(prog.origin, origin, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(prog.zoom, &zoom, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(prog.grid, grid, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(prog.states, &nStates, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(prog.age, &age, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(prog.background, bg, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(prog.lattice, &lattice, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(prog.decayFrom, &decayFrom, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniformSampler(prog.state, stateTexture);
    rlSetUniformSampler(prog.palette, owned_.paletteTex);

    // A quad over the viewport; the fragment shader does the rest. The
    // batch flushes inside EndShaderMode, while `locs` is still alive.
    DrawRectangle(static_cast<int>(vp.x), static_cast<int>(vp.y), static_cast<int>(vp.w), static_cast<int>(vp.h), WHITE);
    EndShaderMode();
    rlDrawRenderBatchActive();
}

}  // namespace aether::render
