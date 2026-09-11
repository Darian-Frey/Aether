#include "sim/gpu_step.hpp"

#include "core/gl.hpp"
#include "sim/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cassert>
#include <format>
#include <string>
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
    for (auto& [key, prog] : programs_) rlUnloadShaderProgram(prog);
}

GpuStepper::GpuStepper(GpuStepper&& o) noexcept
    : programs_(std::move(o.programs_)), program_(o.program_),
      paramsSsbo_(o.paramsSsbo_), offsetsSsbo_(o.offsetsSsbo_), compsSsbo_(o.compsSsbo_), tableSsbo_(o.tableSsbo_),
      target_(o.target_), groupsX_(o.groupsX_), groupsY_(o.groupsY_), groupsZ_(o.groupsZ_),
      width_(o.width_), height_(o.height_), depth_(o.depth_), generation_(o.generation_) {
    o.programs_.clear();
    o.program_ = o.paramsSsbo_ = o.offsetsSsbo_ = o.compsSsbo_ = o.tableSsbo_ = 0;
}

GpuStepper& GpuStepper::operator=(GpuStepper&& o) noexcept {
    if (this != &o) {
        this->~GpuStepper();
        new (this) GpuStepper(std::move(o));
    }
    return *this;
}

void GpuStepper::releaseBuffers() {
    for (unsigned int* b : {&paramsSsbo_, &offsetsSsbo_, &compsSsbo_, &tableSsbo_}) {
        if (*b != 0) rlUnloadShaderBuffer(*b);
        *b = 0;
    }
}

std::optional<core::Error> GpuStepper::compileVariant(const ShapeKey& key) {
    if (programs_.contains(key)) return std::nullopt;
    const auto& [dims, N, S, kind, boundary] = key;

    std::string src = "#version 430\n";
    if (dims == 3) src += "#define AETHER_3D 1\n";
    src += std::format("#define AETHER_N {}\n#define AETHER_S {}\n#define AETHER_KIND {}\n#define AETHER_BOUNDARY {}\n",
                       N, S, static_cast<int>(kind), static_cast<int>(boundary));
    src += "#line 1\n";
    src += shaders::kLutStepComp;

    const unsigned int shader = rlLoadShader(src.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) return core::Error{"lut_step.comp failed to compile (see raylib log)"};
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0) return core::Error{"lut_step.comp failed to link (see raylib log)"};

    programs_[key] = program;
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
    const uint32_t params[4] = {spec.width, spec.height, spec.depth, static_cast<uint32_t>(generation_)};
    std::vector<int32_t> offsets(size_t{N} * 4, 0);
    for (uint32_t i = 0; i < N; ++i) {
        offsets[i * 4 + 0] = rule.offsets[i].dx;
        offsets[i * 4 + 1] = rule.offsets[i].dy;
        offsets[i * 4 + 2] = rule.offsets[i].dz;
    }
    std::vector<uint32_t> table(rule.table.begin(), rule.table.end());

    releaseBuffers();
    paramsSsbo_  = makeSsbo(params, sizeof(params));
    offsetsSsbo_ = makeSsbo(offsets.data(), offsets.size() * sizeof(int32_t));
    compsSsbo_   = makeSsbo(rule.w.data(), rule.w.size() * sizeof(uint32_t));
    tableSsbo_   = makeSsbo(table.data(), table.size() * sizeof(uint32_t));

    program_ = programs_[key];
    target_  = spec.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    width_ = spec.width; height_ = spec.height; depth_ = spec.depth;
    const uint32_t* local = spec.dimensions == 3 ? kLocal3D : kLocal2D;
    groupsX_ = groups(spec.width, local[0]);
    groupsY_ = groups(spec.height, local[1]);
    groupsZ_ = groups(spec.depth, local[2]);
    return std::nullopt;
}

void GpuStepper::step(unsigned int srcTexture, unsigned int dstTexture) {
    assert(program_ != 0 && "setRule before step");
    assert(srcTexture != dstTexture && "step must not read the texture it writes (AV-004)");

    const uint32_t gen = static_cast<uint32_t>(generation_);
    rlUpdateShaderBuffer(paramsSsbo_, &gen, sizeof(gen), 3 * sizeof(uint32_t));

    rlEnableShader(program_);
    glBindImageTexture(0, srcTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  GL_R8UI);
    glBindImageTexture(1, dstTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R8UI);
    rlBindShaderBuffer(paramsSsbo_, 0);
    rlBindShaderBuffer(offsetsSsbo_, 1);
    rlBindShaderBuffer(compsSsbo_, 2);
    rlBindShaderBuffer(tableSsbo_, 3);
    rlComputeShaderDispatch(groupsX_, groupsY_, groupsZ_);
    rlDisableShader();

    // Visible to the next dispatch's imageLoad, to samplers, and to
    // glGetTexImage.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    ++generation_;
}

void GpuStepper::step(core::GpuGrid& grid) {
    assert(grid.target() == target_ && grid.spec().width == width_ &&
           grid.spec().height == height_ && grid.spec().depth == depth_);
    step(grid.current(), grid.next());
    grid.swap();
}

}  // namespace aether::sim
