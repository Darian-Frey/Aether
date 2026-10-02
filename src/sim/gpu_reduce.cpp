#include "sim/gpu_reduce.hpp"

#include "core/gl.hpp"
#include "sim/shaders.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cstring>
#include <format>
#include <utility>

namespace aether::sim {

namespace {

// The field images this shader reads, generated here for the same reason
// gpu_step generates its own: binding points are the stepper's business and not
// the shader's. Read-only and on their own units, because nothing is written.
std::string fieldImagesGlsl(const rule::CompiledRule& rule, uint8_t dimensions) {
    const bool is3D = dimensions == 3;
    const char* suffix = is3D ? "3D" : "2D";
    const std::string coord = is3D ? "p" : "p.xy";

    auto formatOf = [](core::CellType t) {
        switch (t) {
            case core::CellType::F32: return "r32f";
            case core::CellType::U32: return "r32ui";
            case core::CellType::U8:  break;
        }
        return "r8ui";
    };

    std::string out;
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        const bool isFloat = rule.fields[f].cell_type == core::CellType::F32;
        const std::string image = std::format("{}image{}", isFloat ? "" : "u", suffix);
        out += std::format("layout({}, binding = {}) uniform readonly {} aether_rsrc{};\n",
                           formatOf(rule.fields[f].cell_type), 1 + f, image, f);
    }

    // Every field added to the caller's accumulators in field order, which is
    // the order sim::reduce uses too. A u8 or u32 field is counted as its
    // numeric value: a genome total is meaningless but a resource total is the
    // point, and refusing one would mean the readout knew what a field was for.
    out += std::format("void aether_reduce_fields(ivec3 p, inout float sums[{}]) {{\n",
                       rule.fields.size());
    for (size_t f = 0; f < rule.fields.size(); ++f) {
        out += std::format("    sums[{}] += float(imageLoad(aether_rsrc{}, {}).r);\n", f, f, coord);
    }
    out += "}\n";

    if (rule.genome) {
        out += std::format("uint aether_reduce_genome(ivec3 p) {{ return uint(imageLoad(aether_rsrc{}, {}).r); }}\n",
                           rule.genome->field, coord);
    }
    return out;
}

}  // namespace

GpuReducer::~GpuReducer() {
    for (auto& [key, program] : owned_.programs) rlUnloadShaderProgram(program);
    if (owned_.paramsSsbo) rlUnloadShaderBuffer(owned_.paramsSsbo);
    if (owned_.partialsSsbo) rlUnloadShaderBuffer(owned_.partialsSsbo);
}

GpuReducer::GpuReducer(GpuReducer&& o) noexcept
    : owned_(std::exchange(o.owned_, {})), cfg_(o.cfg_), readback_(std::move(o.readback_)) {
    o.cfg_ = {};
}

GpuReducer& GpuReducer::operator=(GpuReducer&& o) noexcept {
    if (this != &o) {
        this->~GpuReducer();
        owned_ = std::exchange(o.owned_, {});
        cfg_ = o.cfg_;
        readback_ = std::move(o.readback_);
        o.cfg_ = {};
    }
    return *this;
}

std::optional<core::Error> GpuReducer::compileVariant(const ShapeKey& key,
                                                      const rule::CompiledRule& rule) {
    if (owned_.programs.contains(key)) return std::nullopt;
    const auto& [dims, states, fields, buckets, genomeShape] = key;
    (void)genomeShape;

    std::string src = "#version 430\n";
    if (dims == 3) src += "#define AETHER_3D 1\n";
    src += std::format("#define AETHER_S {}\n", states);
    src += std::format("#define AETHER_FIELDS {}\n", fields);
    src += std::format("#define AETHER_TILE {}\n", kTileCells);
    src += std::format("#define AETHER_BUCKETS {}\n", buckets);
    src += std::format("#define AETHER_GENOME {}\n", rule.genome ? 1 : 0);
    if (rule.genome) {
        const uint32_t bits = rule.genome->bits;
        src += std::format("#define AETHER_GENOME_F {}\n", rule.genome->field);
        src += std::format("#define AETHER_GENOME_M {}u\n",
                           bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u));
    } else {
        src += "#define AETHER_GENOME_F 0\n#define AETHER_GENOME_M 0u\n";
    }
    std::string body = shaders::kReduceComp;
    if (!rule.fields.empty()) {
        const std::string images = fieldImagesGlsl(rule, dims);
        const std::string marker = "AETHER_FIELD_IMAGES";
        const size_t at = body.find(marker);
        if (at == std::string::npos) return core::Error{"reduce.comp lost its field marker"};
        body.replace(at, marker.size(), images);
    }
    src += "#line 1\n";
    src += body;

    const unsigned int shader = rlLoadShader(src.c_str(), RL_COMPUTE_SHADER);
    if (shader == 0) return core::Error{"reduce.comp failed to compile (see raylib log)"};
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    rlUnloadShader(shader);
    if (program == 0) return core::Error{"reduce.comp failed to link (see raylib log)"};

    owned_.programs[key] = program;
    return std::nullopt;
}

std::optional<core::Error> GpuReducer::setRule(const rule::CompiledRule& rule,
                                               const core::GridSpec& spec) {
    const uint32_t buckets = rule.genome ? kGenomeBuckets : 0;
    const uint64_t genomeShape = rule.genome
        ? (uint64_t{rule.genome->field} << 8) | rule.genome->bits
        : 0;
    const ShapeKey key{spec.dimensions, rule.states,
                       static_cast<uint32_t>(rule.fields.size()), buckets, genomeShape};
    if (auto e = compileVariant(key, rule)) return e;

    cfg_.program = owned_.programs[key];
    cfg_.target = spec.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    cfg_.width = spec.width; cfg_.height = spec.height; cfg_.depth = spec.depth;
    cfg_.states = rule.states;
    cfg_.fields = static_cast<uint32_t>(rule.fields.size());
    cfg_.buckets = buckets;
    cfg_.tiles = static_cast<uint32_t>((spec.cellCount() + kTileCells - 1u) / kTileCells);

    cfg_.fieldFormats.clear();
    for (const rule::CompiledField& f : rule.fields) {
        switch (f.cell_type) {
            case core::CellType::F32: cfg_.fieldFormats.push_back(GL_R32F);  break;
            case core::CellType::U32: cfg_.fieldFormats.push_back(GL_R32UI); break;
            case core::CellType::U8:  cfg_.fieldFormats.push_back(GL_R8UI);  break;
        }
    }

    const uint32_t params[4] = {cfg_.width, cfg_.height, cfg_.depth, cfg_.tiles};
    if (owned_.paramsSsbo) rlUnloadShaderBuffer(owned_.paramsSsbo);
    owned_.paramsSsbo = rlLoadShaderBuffer(sizeof(params), params, RL_DYNAMIC_COPY);

    const size_t stride = size_t{cfg_.states} + cfg_.fields + cfg_.buckets;
    const size_t words = stride * cfg_.tiles;
    if (owned_.partialsSsbo) rlUnloadShaderBuffer(owned_.partialsSsbo);
    owned_.partialsSsbo = rlLoadShaderBuffer(
        static_cast<unsigned int>(words * sizeof(uint32_t)), nullptr, RL_DYNAMIC_COPY);
    readback_.assign(words, 0u);
    return std::nullopt;
}

GridStats GpuReducer::sample(unsigned int stateTexture, std::span<const unsigned int> fieldTextures) {
    GridStats out;
    out.stateCounts.assign(cfg_.states ? cfg_.states : 1u, 0);
    out.fieldTotals.assign(cfg_.fields, 0.0);
    if (cfg_.buckets) out.genomeBuckets.assign(cfg_.buckets, 0);
    if (cfg_.program == 0) return out;

    rlEnableShader(cfg_.program);
    glBindImageTexture(0, stateTexture, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R8UI);
    for (size_t f = 0; f < fieldTextures.size() && f < cfg_.fieldFormats.size(); ++f) {
        glBindImageTexture(static_cast<unsigned int>(1 + f), fieldTextures[f], 0, GL_TRUE, 0,
                           GL_READ_ONLY, cfg_.fieldFormats[f]);
    }
    rlBindShaderBuffer(owned_.paramsSsbo, 0);
    rlBindShaderBuffer(owned_.partialsSsbo, 4);
    // One invocation per tile, local size 1: a tile is summed by one invocation
    // in cell order, which is the contract (sim/stats.hpp). Wider workgroups
    // would need a tree and a tree would need its shape pinning.
    rlComputeShaderDispatch(cfg_.tiles, 1, 1);
    rlDisableShader();

    // The one readback, and it is of the partials rather than of the grid: a few
    // words per tile whatever the grid's size, which is what keeps this on the
    // right side of AV-002.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    rlReadShaderBuffer(owned_.partialsSsbo, readback_.data(),
                       static_cast<unsigned int>(readback_.size() * sizeof(uint32_t)), 0);

    // Tile order, which is the second half of the contract. A float total summed
    // in a different order is a different number, and the host oracle sums the
    // same way.
    const size_t stride = size_t{cfg_.states} + cfg_.fields + cfg_.buckets;
    std::vector<float> sums(cfg_.fields, 0.0f);
    for (uint32_t t = 0; t < cfg_.tiles; ++t) {
        const size_t base = size_t{t} * stride;
        for (uint16_t s = 0; s < cfg_.states; ++s) out.stateCounts[s] += readback_[base + s];
        for (uint32_t f = 0; f < cfg_.fields; ++f) {
            float v = 0.0f;
            const uint32_t bitsValue = readback_[base + cfg_.states + f];
            std::memcpy(&v, &bitsValue, sizeof(float));
            out.fieldTotals[f] += static_cast<double>(v);
        }
        for (uint32_t b = 0; b < cfg_.buckets; ++b) {
            out.genomeBuckets[b] += readback_[base + cfg_.states + cfg_.fields + b];
        }
    }
    return out;
}

}  // namespace aether::sim
