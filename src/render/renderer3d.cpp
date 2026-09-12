#include "render/renderer3d.hpp"

#include "core/gl.hpp"
#include "render/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <utility>

namespace aether::render {

namespace {

// Texture units raylib's batch never touches (it uses 0 and 1..4).
constexpr int kUnitState = 6;
constexpr int kUnitPalette = 7;

}  // namespace

std::variant<Renderer3D, core::Error> Renderer3D::create() {
    Renderer3D r;
    const Shader sh = LoadShaderFromMemory(shaders::kPalette2dVert, shaders::kVolumeFrag);
    if (sh.id == 0 || sh.id == rlGetShaderIdDefault()) {
        return core::Error{"volume shader failed to compile (see raylib log)"};
    }
    r.owned_.shader = sh.id;
    auto& c = r.cfg_;
    c.locState      = GetShaderLocation(sh, "stateTex");
    c.locPalette    = GetShaderLocation(sh, "paletteTex");
    c.locFrame      = GetShaderLocation(sh, "frameSize");
    c.locViewport   = GetShaderLocation(sh, "viewport");
    c.locCamPos     = GetShaderLocation(sh, "camPos");
    c.locForward    = GetShaderLocation(sh, "camForward");
    c.locRight      = GetShaderLocation(sh, "camRight");
    c.locUp         = GetShaderLocation(sh, "camUp");
    c.locTanHalf    = GetShaderLocation(sh, "tanHalfFov");
    c.locGrid       = GetShaderLocation(sh, "gridSize");
    c.locClipMin    = GetShaderLocation(sh, "clipMin");
    c.locClipMax    = GetShaderLocation(sh, "clipMax");
    c.locOpacity    = GetShaderLocation(sh, "opacity");
    c.locMaxSteps   = GetShaderLocation(sh, "maxSteps");
    c.locBackground = GetShaderLocation(sh, "background");
    RL_FREE(sh.locs);

    r.owned_.paletteTex = rlLoadTexture(nullptr, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    if (r.owned_.paletteTex == 0) {
        rlUnloadShaderProgram(r.owned_.shader);
        return core::Error{"palette texture allocation failed"};
    }
    rlTextureParameters(r.owned_.paletteTex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(r.owned_.paletteTex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    r.setPalette(Palette::defaultFor(2));
    return r;
}

Renderer3D::Renderer3D(Renderer3D&& o) noexcept
    : owned_(std::exchange(o.owned_, Owned{})), cfg_(o.cfg_) {}

Renderer3D& Renderer3D::operator=(Renderer3D&& o) noexcept {
    if (this != &o) {
        release();
        owned_ = std::exchange(o.owned_, Owned{});
        cfg_ = o.cfg_;
    }
    return *this;
}

Renderer3D::~Renderer3D() { release(); }

void Renderer3D::release() {
    if (owned_.shader != 0) rlUnloadShaderProgram(owned_.shader);
    if (owned_.paletteTex != 0) rlUnloadTexture(owned_.paletteTex);
    owned_ = Owned{};
}

void Renderer3D::setPalette(const Palette& p) {
    cfg_.palette = p;
    rlUpdateTexture(owned_.paletteTex, 0, 0, 256, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, cfg_.palette.entries.data());
}

void Renderer3D::draw(unsigned int stateTexture, const core::GridSpec& spec, const Orbit& camera,
                      const VolumeSettings& settings, const Rect& vp, int frameWidth, int frameHeight) {
    const auto [forward, right, up] = camera.basis();
    const Vec3 pos = camera.position();
    const float frame[2]    = {static_cast<float>(frameWidth), static_cast<float>(frameHeight)};
    const float viewport[4] = {vp.x, vp.y, vp.w, vp.h};
    const float camPos[3]   = {static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)};
    const float fwd[3]      = {static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z)};
    const float rgt[3]      = {static_cast<float>(right.x), static_cast<float>(right.y), static_cast<float>(right.z)};
    const float upv[3]      = {static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z)};
    const float tanHalf     = static_cast<float>(std::tan(camera.fovY * 0.5));
    const float grid[3]     = {static_cast<float>(spec.width), static_cast<float>(spec.height), static_cast<float>(spec.depth)};
    const double ext[3]     = {double(spec.width), double(spec.height), double(spec.depth)};
    float clipMin[3], clipMax[3];
    for (int a = 0; a < 3; ++a) {
        clipMin[a] = static_cast<float>(std::clamp(settings.clipMin[static_cast<size_t>(a)], 0.0, ext[a]));
        const double mx = settings.clipMax[static_cast<size_t>(a)] <= 0.0 ? ext[a] : settings.clipMax[static_cast<size_t>(a)];
        clipMax[a] = static_cast<float>(std::clamp(mx, 0.0, ext[a]));
    }
    const float opacity     = static_cast<float>(settings.opacity);
    const int   maxSteps    = static_cast<int>(spec.width + spec.height + spec.depth + 3);
    const float bg[4]       = {cfg_.background.r / 255.0f, cfg_.background.g / 255.0f, cfg_.background.b / 255.0f, 1.0f};

    Shader sh{};
    sh.id = owned_.shader;
    int locs[RL_MAX_SHADER_LOCATIONS];
    for (int& l : locs) l = -1;
    locs[SHADER_LOC_MATRIX_MVP]        = rlGetLocationUniform(owned_.shader, "mvp");
    locs[SHADER_LOC_VERTEX_POSITION]   = rlGetLocationAttrib(owned_.shader, "vertexPosition");
    locs[SHADER_LOC_VERTEX_TEXCOORD01] = rlGetLocationAttrib(owned_.shader, "vertexTexCoord");
    locs[SHADER_LOC_VERTEX_COLOR]      = rlGetLocationAttrib(owned_.shader, "vertexColor");
    sh.locs = locs;

    rlDrawRenderBatchActive();
    BeginShaderMode(sh);
    rlEnableShader(owned_.shader);
    rlSetUniform(cfg_.locFrame, frame, RL_SHADER_UNIFORM_VEC2, 1);
    rlSetUniform(cfg_.locViewport, viewport, RL_SHADER_UNIFORM_VEC4, 1);
    rlSetUniform(cfg_.locCamPos, camPos, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locForward, fwd, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locRight, rgt, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locUp, upv, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locTanHalf, &tanHalf, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(cfg_.locGrid, grid, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locClipMin, clipMin, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locClipMax, clipMax, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(cfg_.locOpacity, &opacity, RL_SHADER_UNIFORM_FLOAT, 1);
    rlSetUniform(cfg_.locMaxSteps, &maxSteps, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(cfg_.locBackground, bg, RL_SHADER_UNIFORM_VEC4, 1);

    // raylib's sampler registration binds GL_TEXTURE_2D, which a 3D texture
    // cannot use; bind both textures by hand on units the batch ignores.
    const int unitState = kUnitState, unitPalette = kUnitPalette;
    glActiveTexture(GL_TEXTURE0 + kUnitState);
    glBindTexture(GL_TEXTURE_3D, stateTexture);
    glActiveTexture(GL_TEXTURE0 + kUnitPalette);
    glBindTexture(GL_TEXTURE_2D, owned_.paletteTex);
    glActiveTexture(GL_TEXTURE0);
    rlSetUniform(cfg_.locState, &unitState, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(cfg_.locPalette, &unitPalette, RL_SHADER_UNIFORM_INT, 1);

    DrawRectangle(static_cast<int>(vp.x), static_cast<int>(vp.y), static_cast<int>(vp.w), static_cast<int>(vp.h), WHITE);
    EndShaderMode();
    rlDrawRenderBatchActive();
}

}  // namespace aether::render
