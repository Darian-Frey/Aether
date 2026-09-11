#include "core/gpu_grid.hpp"

#include "core/gl.hpp"

#include <algorithm>
#include <format>
#include <string_view>

namespace aether::core {

namespace {

// Vendor extension enums; raylib's glad is core-profile only and does not
// carry them.
constexpr GLenum kNvxCurrentAvailableVidmem = 0x9049;   // GL_NVX_gpu_memory_info, in KB
constexpr GLenum kAtiTextureFreeMemory       = 0x87FC;   // GL_ATI_meminfo, in KB (4 values)

bool hasExtension(std::string_view name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && name == ext) return true;
    }
    return false;
}

GLenum internalFormat(CellType t) { return t == CellType::F32 ? GL_R32F : GL_R8UI; }
GLenum transferFormat(CellType t) { return t == CellType::F32 ? GL_RED : GL_RED_INTEGER; }
GLenum transferType(CellType t)   { return t == CellType::F32 ? GL_FLOAT : GL_UNSIGNED_BYTE; }

}  // namespace

VramInfo queryVram() {
    VramInfo info;
    if (hasExtension("GL_NVX_gpu_memory_info")) {
        GLint kb = 0;
        glGetIntegerv(kNvxCurrentAvailableVidmem, &kb);
        info.available_bytes = static_cast<uint64_t>(kb) * 1024;
        info.source = "NVX";
    } else if (hasExtension("GL_ATI_meminfo")) {
        GLint kb[4] = {0, 0, 0, 0};
        glGetIntegerv(kAtiTextureFreeMemory, kb);
        info.available_bytes = static_cast<uint64_t>(kb[0]) * 1024;
        info.source = "ATI";
    } else {
        info.source = "unknown";
    }
    return info;
}

std::optional<Error> checkFootprint(const GridSpec& spec, const VramInfo& vram) {
    if (!vram.available_bytes) return std::nullopt;
    const uint64_t needed = spec.footprintBytes() + spec.footprintBytes() / 4;
    if (needed > *vram.available_bytes) {
        return Error{std::format(
            "grid {}x{}x{} needs {:.1f} MB with headroom; {:.1f} MB of VRAM available ({})",
            spec.width, spec.height, spec.depth,
            static_cast<double>(needed) / 1048576.0,
            static_cast<double>(*vram.available_bytes) / 1048576.0,
            vram.source)};
    }
    return std::nullopt;
}

std::optional<Error> checkTextureLimits(const GridSpec& spec) {
    GLint max2d = 0, max3d = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max2d);
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max3d);
    const uint64_t limit = spec.dimensions == 3 ? static_cast<uint64_t>(max3d)
                                                : static_cast<uint64_t>(max2d);
    const uint64_t largest = std::max({uint64_t{spec.width}, uint64_t{spec.height}, uint64_t{spec.depth}});
    if (largest > limit) {
        return Error{std::format("extent {} exceeds the driver's {} texture limit of {}",
                                 largest, spec.dimensions == 3 ? "3D" : "2D", limit)};
    }
    return std::nullopt;
}

std::variant<GpuGrid, Error> GpuGrid::create(const GridSpec& spec, const VramInfo& vram) {
    if (const auto problems = spec.problems(); !problems.empty()) {
        return Error{problems.front()};
    }
    if (auto e = checkTextureLimits(spec)) return *e;
    if (auto e = checkFootprint(spec, vram)) return *e;

    const GLenum target = spec.dimensions == 3 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    while (glGetError() != GL_NO_ERROR) {}   // discard anything stale

    GLuint tex[2] = {0, 0};
    glGenTextures(2, tex);
    for (GLuint id : tex) {
        glBindTexture(target, id);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (target == GL_TEXTURE_3D) {
            glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexStorage3D(target, 1, internalFormat(spec.cell_type),
                           static_cast<GLsizei>(spec.width), static_cast<GLsizei>(spec.height),
                           static_cast<GLsizei>(spec.depth));
        } else {
            glTexStorage2D(target, 1, internalFormat(spec.cell_type),
                           static_cast<GLsizei>(spec.width), static_cast<GLsizei>(spec.height));
        }
    }
    glBindTexture(target, 0);

    // glTexStorage is where an out-of-memory surfaces on drivers that report
    // no availability figure at all.
    if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
        glDeleteTextures(2, tex);
        return Error{std::format("texture allocation failed (GL error 0x{:x})", err)};
    }

    GpuGrid grid(spec, target, tex[0], tex[1]);
    const std::vector<uint8_t> zero(spec.bytesPerBuffer(), 0);
    grid.upload(zero);
    grid.swap();
    grid.upload(zero);
    grid.swap();
    return grid;
}

GpuGrid::GpuGrid(GridSpec spec, unsigned int target, unsigned int a, unsigned int b)
    : spec_(spec), target_(target), pair_(a, b) {}

GpuGrid::GpuGrid(GpuGrid&& o) noexcept
    : spec_(o.spec_), target_(o.target_), pair_(o.pair_) {
    o.pair_ = PingPong<unsigned int>(0, 0);
}

GpuGrid& GpuGrid::operator=(GpuGrid&& o) noexcept {
    if (this != &o) {
        release();
        spec_ = o.spec_;
        target_ = o.target_;
        pair_ = o.pair_;
        o.pair_ = PingPong<unsigned int>(0, 0);
    }
    return *this;
}

GpuGrid::~GpuGrid() {
    release();
}

void GpuGrid::release() {
    const GLuint tex[2] = {pair_.current(), pair_.next()};
    if (tex[0] != 0 || tex[1] != 0) glDeleteTextures(2, tex);
    pair_ = PingPong<unsigned int>(0, 0);
}

unsigned int GpuGrid::format() const {
    return internalFormat(spec_.cell_type);
}

void GpuGrid::upload(std::span<const uint8_t> cells) {
    glBindTexture(target_, pair_.current());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (target_ == GL_TEXTURE_3D) {
        glTexSubImage3D(target_, 0, 0, 0, 0,
                        static_cast<GLsizei>(spec_.width), static_cast<GLsizei>(spec_.height),
                        static_cast<GLsizei>(spec_.depth),
                        transferFormat(spec_.cell_type), transferType(spec_.cell_type), cells.data());
    } else {
        glTexSubImage2D(target_, 0, 0, 0,
                        static_cast<GLsizei>(spec_.width), static_cast<GLsizei>(spec_.height),
                        transferFormat(spec_.cell_type), transferType(spec_.cell_type), cells.data());
    }
    glBindTexture(target_, 0);
}

void GpuGrid::uploadRegion(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h, uint32_t d,
                           std::span<const uint8_t> cells) {
    glBindTexture(target_, pair_.current());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (target_ == GL_TEXTURE_3D) {
        glTexSubImage3D(target_, 0, static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLint>(z),
                        static_cast<GLsizei>(w), static_cast<GLsizei>(h), static_cast<GLsizei>(d),
                        transferFormat(spec_.cell_type), transferType(spec_.cell_type), cells.data());
    } else {
        glTexSubImage2D(target_, 0, static_cast<GLint>(x), static_cast<GLint>(y),
                        static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                        transferFormat(spec_.cell_type), transferType(spec_.cell_type), cells.data());
    }
    glBindTexture(target_, 0);
}

void GpuGrid::download(std::span<uint8_t> cells) const {
    glBindTexture(target_, pair_.current());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(target_, 0, transferFormat(spec_.cell_type), transferType(spec_.cell_type), cells.data());
    glBindTexture(target_, 0);
}

}  // namespace aether::core
