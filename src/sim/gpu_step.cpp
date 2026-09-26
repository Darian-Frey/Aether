#include "sim/gpu_step.hpp"

#include "core/gl.hpp"
#include "rule/glsl.hpp"
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

// How often the step loop drains the GPU queue. Not a tuning knob: without a
// periodic sync the results are wrong, not merely late (BUG-011).
constexpr uint64_t kSyncEvery = 64;

// The shader's local_size, which must match the layout() in lut_step.comp.
constexpr uint32_t kLocal2D[3] = {8, 8, 1};
constexpr uint32_t kLocal3D[3] = {4, 4, 4};

uint32_t groups(uint32_t extent, uint32_t local) {
    return (extent + local - 1) / local;
}

// Image units. 0 and 1 are the state's pair; a field's pair follows, read at
// 2 + 2f and written at 3 + 2f. GL guarantees only eight units to a compute
// shader, which is what bounds the number of fields (see setRule).
constexpr uint32_t kStateUnits = 2;
uint32_t fieldReadUnit(size_t f)  { return kStateUnits + 2u * static_cast<uint32_t>(f); }
uint32_t fieldWriteUnit(size_t f) { return kStateUnits + 2u * static_cast<uint32_t>(f) + 1u; }

// The GLSL a multi-field rule needs around the functions rule/glsl generated:
// the field images, a loader apiece, and the two entry points lut_step.comp
// calls. It lives here rather than in rule/ because image bindings are this
// file's business and not the IR's (F-031, D-022). Every name shared with the
// other generator comes from rule/glsl.hpp; nothing here spells one.
std::string fieldSupportGlsl(const rule::CompiledRule& rule, uint8_t dimensions) {
    const bool is3D = dimensions == 3;
    const char* suffix = is3D ? "3D" : "2D";
    // imageLoad/imageStore take the coordinate the image's dimensionality
    // wants, and a 1D or 2D grid is a 2D image (see readCell in the shader).
    const std::string coord = is3D ? "p" : "p.xy";
    const uint32_t N = rule.neighbourCount();

    std::string out;
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        const bool isFloat = rule.fields[f].cell_type == core::CellType::F32;
        const char* format  = isFloat ? "r32f" : "r8ui";
        const std::string image = std::format("{}image{}", isFloat ? "" : "u", suffix);
        out += std::format("layout({}, binding = {}) uniform readonly  {} aether_fsrc{};\n",
                           format, fieldReadUnit(f), image, f);
        out += std::format("layout({}, binding = {}) uniform writeonly {} aether_fdst{};\n",
                           format, fieldWriteUnit(f), image, f);
    }
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        out += std::format("{} aether_fload{}(ivec3 p) {{ return {}(imageLoad(aether_fsrc{}, {}).r); }}\n",
                           rule::glslFieldType(rule.fields[f].cell_type), f,
                           rule::glslFieldType(rule.fields[f].cell_type), f, coord);
    }

    // The gather. A negative x is the sentinel the shader's resolveCoord
    // produces for "outside a zero boundary", where a field reads zero exactly
    // as state 0 does.
    out += std::format("{} aether_gather_fields(ivec3 p, ivec3 n[{}]) {{\n", rule::glslFieldsStruct(), N);
    out += std::format("    {} f;\n", rule::glslFieldsStruct());
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        const bool isFloat = rule.fields[f].cell_type == core::CellType::F32;
        out += std::format("    f.{} = aether_fload{}(p);\n", rule::glslFieldSelfMember(f), f);
        out += std::format("    for (int i = 0; i < {}; ++i) f.{}[i] = (n[i].x < 0) ? {} : aether_fload{}(n[i]);\n",
                           N, rule::glslFieldNbrMember(f), isFloat ? "0.0" : "0", f);
    }
    out += "    return f;\n}\n";

    // The store. A field the rule writes goes through its generated function;
    // one it does not is copied from the gather, because the destination
    // texture is last generation's and would otherwise be read back as the
    // cell's own great-grandparent.
    out += std::format("void aether_store_fields(ivec3 p, uint own, uint nbr[{}], {} fld) {{\n",
                       N, rule::glslFieldsStruct());
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        const bool isFloat = rule.fields[f].cell_type == core::CellType::F32;
        const std::string value = rule.fields[f].write
                                      ? std::format("{}(own, nbr, fld)", rule::glslFieldFunction(f))
                                      : std::format("fld.{}", rule::glslFieldSelfMember(f));
        if (isFloat) {
            out += std::format("    imageStore(aether_fdst{}, {}, vec4({}, 0.0, 0.0, 0.0));\n", f, coord, value);
        } else {
            out += std::format("    imageStore(aether_fdst{}, {}, uvec4(uint({}), 0u, 0u, 0u));\n", f, coord, value);
        }
    }
    out += "}\n";
    return out;
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

std::optional<core::Error> GpuStepper::compileVariant(const ShapeKey& key, const rule::CompiledRule& rule) {
    if (owned_.programs.contains(key)) return std::nullopt;
    const auto& [dims, N, S, kind, boundary, hash] = key;
    (void)hash;

    std::string src = "#version 430\n";
    if (dims == 3) src += "#define AETHER_3D 1\n";
    src += std::format("#define AETHER_N {}\n#define AETHER_S {}\n#define AETHER_KIND {}\n#define AETHER_BOUNDARY {}\n",
                       N, S, static_cast<int>(kind), static_cast<int>(boundary));
    // Always defined, so the shader's `#if AETHER_FIELDS > 0` does not rest on
    // how the preprocessor treats an unknown identifier.
    src += std::format("#define AETHER_FIELDS {}\n", rule.fields.size());
    // SPEC §6's fourth agreement rule, for the float arithmetic the shaders do
    // themselves rather than through a generated function — the convolution.
    // The number comes from rule/glsl so that it is written once (BUG-021).
    src += std::format("#define AETHER_FTZ(v) ((abs(v) < {}) ? 0.0 : (v))\n", rule::glslSubnormalMin());
    src += shaders::kHashGlsl;
    // A generated rule arrives as the function the step calls (SPEC §6):
    // aether_rule for a table-less discrete rule, aether_rule_f for a kernel.
    if (rule.backend == rule::Backend::Codegen) src += rule.glsl;
    // Then the field images and the two entry points over them, which must
    // follow rule.glsl because they call the functions in it.
    if (!rule.fields.empty()) src += fieldSupportGlsl(rule, dims);
    src += "#line 1\n";
    const bool continuous = kind == rule::Kind::Continuous;
    src += continuous ? shaders::kContinuousStepComp : shaders::kLutStepComp;
    const char* name = continuous ? "continuous_step.comp" : "lut_step.comp";

    const unsigned int shader = rlLoadShader(src.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) return core::Error{std::format("{} failed to compile (see raylib log)", name)};
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0) return core::Error{std::format("{} failed to link (see raylib log)", name)};

    owned_.programs[key] = program;
    return std::nullopt;
}

std::optional<core::Error> GpuStepper::setRule(const rule::CompiledRule& rule, const core::GridSpec& spec) {
    if (rule.dimensions != spec.dimensions) {
        return core::Error{std::format("rule is {}D but the grid is {}D", rule.dimensions, spec.dimensions)};
    }
    if (!rule.fields.empty()) {
        // Each field needs a read unit and a write unit on top of the state's
        // two, and GL_MAX_IMAGE_UNITS is only guaranteed to be eight. Asked
        // rather than assumed, and refused rather than left to a link error
        // whose message would name a binding number instead of a field.
        GLint maxUnits = 8;
        glGetIntegerv(GL_MAX_IMAGE_UNITS, &maxUnits);
        const size_t needed = kStateUnits + 2 * rule.fields.size();
        if (needed > static_cast<size_t>(maxUnits)) {
            return core::Error{std::format(
                "{} fields need {} image units and this driver offers {}",
                rule.fields.size(), needed, maxUnits)};
        }
        if (rule.kind == rule::Kind::Continuous) {
            // compileRule refuses this already; repeated here because the
            // continuous shader has no field hooks at all and a change that
            // relaxed the other refusal should trip on this one.
            return core::Error{"a continuous rule cannot carry auxiliary fields"};
        }
    }
    const bool continuous = rule.kind == rule::Kind::Continuous;
    if ((spec.cell_type == core::CellType::F32) != continuous) {
        return core::Error{std::format("a {} grid cannot run a {} rule",
                                       core::toString(spec.cell_type), rule::toString(rule.kind))};
    }
    const uint32_t N = rule.neighbourCount();
    const ShapeKey key{rule.dimensions, N, rule.states, rule.kind, rule.boundary,
                       rule.backend == rule::Backend::Codegen ? rule.ir_hash : 0};
    if (auto e = compileVariant(key, rule)) return e;

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
    owned_.compsSsbo   = makeSsbo(rule.aux.data(), rule.aux.size() * sizeof(uint32_t));
    // Binding 3 carries the table for a discrete rule and the kernel weights
    // for a continuous one; the two shaders never share a program.
    owned_.tableSsbo   = continuous ? makeSsbo(rule.weights.data(), rule.weights.size() * sizeof(float))
                                    : makeSsbo(table.data(), table.size() * sizeof(uint32_t));

    cfg_.program = owned_.programs[key];
    cfg_.locGenLo     = rlGetLocationUniform(cfg_.program, "generationLo");
    cfg_.locGenHi     = rlGetLocationUniform(cfg_.program, "generationHi");
    cfg_.locThreshold = rlGetLocationUniform(cfg_.program, "mutationThreshold");
    cfg_.locSeedLo    = rlGetLocationUniform(cfg_.program, "seedBLo");
    cfg_.locSeedHi    = rlGetLocationUniform(cfg_.program, "seedBHi");
    cfg_.locBlockShift = rlGetLocationUniform(cfg_.program, "mutationBlockShift");
    cfg_.locSelfWeight = continuous ? rlGetLocationUniform(cfg_.program, "selfWeight") : -1;
    cfg_.selfWeight = rule.selfWeight;
    cfg_.continuous = continuous;
    cfg_.fieldFormats.clear();
    for (const rule::CompiledField& f : rule.fields) {
        cfg_.fieldFormats.push_back(f.cell_type == core::CellType::F32 ? GL_R32F : GL_R8UI);
    }
    cfg_.target = spec.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    cfg_.width = spec.width; cfg_.height = spec.height; cfg_.depth = spec.depth;
    const uint32_t* local = spec.dimensions == 3 ? kLocal3D : kLocal2D;
    cfg_.groupsX = groups(spec.width, local[0]);
    cfg_.groupsY = groups(spec.height, local[1]);
    cfg_.groupsZ = groups(spec.depth, local[2]);
    return std::nullopt;
}

void GpuStepper::step(unsigned int srcTexture, unsigned int dstTexture,
                     std::span<const FieldTextures> fields) {
    assert(cfg_.program != 0 && "setRule before step");
    assert(srcTexture != dstTexture && "step must not read the texture it writes (AV-004)");
    assert(fields.size() == cfg_.fieldFormats.size() && "one texture pair per declared field");

    rlEnableShader(cfg_.program);
    // Per-step values as uniforms (see the shader for why not a buffer).
    const uint32_t genLo = static_cast<uint32_t>(cfg_.generation), genHi = static_cast<uint32_t>(cfg_.generation >> 32);
    const uint32_t seedLo = static_cast<uint32_t>(cfg_.mutation.seedB), seedHi = static_cast<uint32_t>(cfg_.mutation.seedB >> 32);
    rlSetUniform(cfg_.locGenLo, &genLo, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locGenHi, &genHi, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locThreshold, &cfg_.mutation.threshold, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locSeedLo, &seedLo, RL_SHADER_UNIFORM_UINT, 1);
    rlSetUniform(cfg_.locSeedHi, &seedHi, RL_SHADER_UNIFORM_UINT, 1);
    const uint32_t blockShift = cfg_.mutation.blockShift;
    rlSetUniform(cfg_.locBlockShift, &blockShift, RL_SHADER_UNIFORM_UINT, 1);
    if (cfg_.continuous) rlSetUniform(cfg_.locSelfWeight, &cfg_.selfWeight, RL_SHADER_UNIFORM_FLOAT, 1);
    const unsigned int format = cfg_.continuous ? GL_R32F : GL_R8UI;
    glBindImageTexture(0, srcTexture, 0, GL_TRUE, 0, GL_READ_ONLY,  format);
    glBindImageTexture(1, dstTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, format);
    for (size_t f = 0; f < fields.size(); ++f) {
        assert(fields[f].src != fields[f].dst && "a field must not be read and written at once (AV-004)");
        glBindImageTexture(fieldReadUnit(f),  fields[f].src, 0, GL_TRUE, 0, GL_READ_ONLY,  cfg_.fieldFormats[f]);
        glBindImageTexture(fieldWriteUnit(f), fields[f].dst, 0, GL_TRUE, 0, GL_WRITE_ONLY, cfg_.fieldFormats[f]);
    }
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
    // Drain the queue periodically. Left to itself, a run that never
    // synchronises — headless, or a burst — queues thousands of dispatches and
    // the results eventually come back wrong: Mesa returned an all-zero grid,
    // NVIDIA a grid with its first cells corrupt and the field collapsed
    // (BUG-011). The interactive path never showed it because `App` already
    // calls glFinish once a frame for the vsync throttle, which is this by
    // accident. Every 64 generations is as good as every one and costs a
    // fraction of a percent of throughput.
    if (cfg_.generation % kSyncEvery == 0) glFinish();
    ++cfg_.generation;
}

void GpuStepper::step(core::GpuGrid& grid) {
    assert(grid.target() == cfg_.target && grid.spec().width == cfg_.width &&
           grid.spec().height == cfg_.height && grid.spec().depth == cfg_.depth);
    step(grid.current(), grid.next());
    grid.swap();
}

}  // namespace aether::sim
