#include "sim/gpu_step.hpp"

#include "core/gl.hpp"
#include "sim/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cassert>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace aether::sim {

namespace {

// The shader's local_size, which must match the layout() in lut_step.comp.
constexpr uint32_t kLocal2D[3] = {8, 8, 1};
constexpr uint32_t kLocal3D[3] = {4, 4, 4};

uint32_t groups(uint32_t extent, uint32_t local) {
    return (extent + local - 1) / local;
}

unsigned int makeSsbo(const void* data, size_t bytes) {
    // rlLoadShaderBuffer rejects a zero size; a kind with no W table still
    // needs a bound buffer at that slot.
    static const uint32_t kZero = 0;
    if (bytes == 0) return rlLoadShaderBuffer(sizeof(kZero), &kZero, RL_STATIC_READ);
    return rlLoadShaderBuffer(static_cast<unsigned int>(bytes), data, RL_STATIC_READ);
}

}  // namespace

GpuStepper::~GpuStepper() {
    releaseBuffers();
    for (auto& [key, prog] : owned_.programs) rlUnloadShaderProgram(prog);
}

GpuStepper::GpuStepper(GpuStepper&& o) noexcept
    : owned_(std::exchange(o.owned_, Owned{})), cfg_(o.cfg_) {
    o.cfg_.program = 0;
}

GpuStepper& GpuStepper::operator=(GpuStepper&& o) noexcept {
    if (this != &o) {
        releaseBuffers();
        for (auto& [key, prog] : owned_.programs) rlUnloadShaderProgram(prog);
        owned_ = std::exchange(o.owned_, Owned{});
        cfg_ = o.cfg_;
        o.cfg_.program = 0;
    }
    return *this;
}

void GpuStepper::releaseBuffers() {
    for (unsigned int* b : {&owned_.paramsSsbo, &owned_.offsetsSsbo, &owned_.compsSsbo, &owned_.tableSsbo}) {
        if (*b != 0) rlUnloadShaderBuffer(*b);
        *b = 0;
    }
}

std::optional<core::Error> GpuStepper::compileVariant(const ShapeKey& key) {
    if (owned_.programs.contains(key)) return std::nullopt;
    const auto& [dims, N, S, kind, boundary] = key;

    std::string src = "#version 430\n";
    if (dims == 3) src += "#define AETHER_3D 1\n";
    src += std::format("#define AETHER_N {}\n#define AETHER_S {}\n#define AETHER_KIND {}\n#define AETHER_BOUNDARY {}\n",
                       N, S, static_cast<int>(kind), static_cast<int>(boundary));
    src += shaders::kHashGlsl;
    src += "#line 1\n";
    src += shaders::kLutStepComp;

    const unsigned int shader = rlLoadShader(src.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) return core::Error{"lut_step.comp failed to compile (see raylib log)"};
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0) return core::Error{"lut_step.comp failed to link (see raylib log)"};

    owned_.programs[key] = program;
    return std::nullopt;
}

std::optional<core::Error> GpuStepper::setRule(const rule::LutRule& rule, const core::GridSpec& spec) {
    if (rule.dimensions != spec.dimensions) {
        return core::Error{std::format("rule is {}D but the grid is {}D", rule.dimensions, spec.dimensions)};
    }
    if (spec.cell_type != core::CellType::U8) {
        return core::Error{"the table backend steps u8 grids only"};
    }
    const uint32_t N = rule.neighbourCount();
    const ShapeKey key{rule.dimensions, N, rule.states, rule.kind, rule.boundary};
    if (auto e = compileVariant(key)) return e;

    // Build every buffer before touching the active set, so a failure
    // above leaves the previous rule running untouched.
    const uint32_t params[3] = {spec.width, spec.height, spec.depth};
    std::vector<int32_t> offsets(size_t{N} * 4, 0);
    for (uint32_t i = 0; i < N; ++i) {
        offsets[i * 4 + 0] = rule.offsets[i].dx;
        offsets[i * 4 + 1] = rule.offsets[i].dy;
        offsets[i * 4 + 2] = rule.offsets[i].dz;
    }
    std::vector<uint32_t> table(rule.table.begin(), rule.table.end());

    releaseBuffers();
    owned_.paramsSsbo  = makeSsbo(params, sizeof(params));
    owned_.offsetsSsbo = makeSsbo(offsets.data(), offsets.size() * sizeof(int32_t));
    owned_.compsSsbo   = makeSsbo(rule.w.data(), rule.w.size() * sizeof(uint32_t));
    owned_.tableSsbo   = makeSsbo(table.data(), table.size() * sizeof(uint32_t));

    cfg_.program = owned_.programs[key];
    cfg_.locGenLo     = rlGetLocationUniform(cfg_.program, "generationLo");
    cfg_.locGenHi     = rlGetLocationUniform(cfg_.program, "generationHi");
    cfg_.locThreshold = rlGetLocationUniform(cfg_.program, "mutationThreshold");
    cfg_.locSeedLo    = rlGetLocationUniform(cfg_.program, "seedBLo");
    cfg_.locSeedHi    = rlGetLocationUniform(cfg_.program, "seedBHi");
    cfg_.target = spec.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    cfg_.width = spec.width; cfg_.height = spec.height; cfg_.depth = spec.depth;
    const uint32_t* local = spec.dimensions == 3 ? kLocal3D : kLocal2D;
    cfg_.groupsX = groups(spec.width, local[0]);
    cfg_.groupsY = groups(spec.height, local[1]);
    cfg_.groupsZ = groups(spec.depth, local[2]);
    return std::nullopt;
}

void GpuStepper::step(unsigned int srcTexture, unsigned int dstTexture) {
    assert(cfg_.program != 0 && "setRule before step");
    assert(srcTexture != dstTexture && "step must not read the texture it writes (AV-004)");

    rlEnableShader(cfg_.program);
    // Per-step values as uniforms (see the shader for why not a buffer).
    const uint32_t genLo = static_cast<uint32_t>(cfg_.generation), genHi = static_cast<uint32_t>(cfg_.generation >> 32);
    const uint32_t seedLo = static_cast<uint32_t>(cfg_.mutation.seedB), seedHi = static_cast<uint32_t>(cfg_.mutation.seedB >> 32);
    rlSetUniform(cfg_.locGenLo, &genLo, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locGenHi, &genHi, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locThreshold, &cfg_.mutation.threshold, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locSeedLo, &seedLo, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locSeedHi, &seedHi, RL_SHADER_UNIFORM_UINT, 1);
    glBindImageTexture(0, srcTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_R8UI);
    glBindImageTexture(1, dstTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R8UI);
    rlBindShaderBuffer(owned_.paramsSsbo, 0);
    rlBindShaderBuffer(owned_.offsetsSsbo, 1);
    rlBindShaderBuffer(owned_.compsSsbo, 2);
    rlBindShaderBuffer(owned_.tableSsbo, 3);
    rlComputeShaderDispatch(cfg_.groupsX, cfg_.groupsY, cfg_.groupsZ);
    rlDisableShader();

    // Visible to the next dispatch's imageLoad, to samplers, and to
    // glGetTexImage.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    ++cfg_.generation;
}

void GpuStepper::step(core::GpuGrid& grid) {
    assert(grid.target() == cfg_.target && grid.spec().width == cfg_.width &&
           grid.spec().height == cfg_.height && grid.spec().depth == cfg_.depth);
    step(grid.current(), grid.next());
    grid.swap();
}

}  // namespace aether::sim
