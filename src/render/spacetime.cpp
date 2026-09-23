#include "render/spacetime.hpp"

#include "core/gl.hpp"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace aether::render {

std::variant<SpaceTime, core::Error> SpaceTime::create(uint32_t width, uint32_t rows) {
    if (width == 0 || rows == 0) return core::Error{"a space-time view needs at least one row"};

    while (glGetError() != GL_NO_ERROR) {}   // discard anything stale
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R8UI, static_cast<GLsizei>(width), static_cast<GLsizei>(rows));
    glBindTexture(GL_TEXTURE_2D, 0);
    if (const GLenum err = glGetError(); err != GL_NO_ERROR) {
        glDeleteTextures(1, &tex);
        return core::Error{std::format("space-time texture allocation failed (GL error 0x{:x})", err)};
    }

    SpaceTime st;
    st.texture_ = tex;
    st.width_ = width;
    st.rows_ = rows;
    st.clear();
    return st;
}

SpaceTime::SpaceTime(SpaceTime&& o) noexcept { *this = std::move(o); }

SpaceTime& SpaceTime::operator=(SpaceTime&& o) noexcept {
    if (this != &o) {
        release();
        texture_ = std::exchange(o.texture_, 0);
        width_ = o.width_; rows_ = o.rows_; next_ = o.next_; captured_ = o.captured_;
    }
    return *this;
}

SpaceTime::~SpaceTime() { release(); }

void SpaceTime::release() {
    if (texture_ != 0) glDeleteTextures(1, &texture_);
    texture_ = 0;
}

void SpaceTime::clear() {
    // glClearTexImage is GL 4.4 and this is a 4.3 engine, so the blank is
    // uploaded. It happens on a reset, never per frame.
    const std::vector<uint8_t> blank(static_cast<size_t>(width_) * rows_, 0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(rows_),
                    GL_RED_INTEGER, GL_UNSIGNED_BYTE, blank.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    next_ = 0;
    captured_ = 0;
}

void SpaceTime::capture(unsigned int gridTexture) {
    if (texture_ == 0 || gridTexture == 0) return;
    // Texture to texture: the generation never comes back to the host, which
    // is what keeps a 1D run free of the readback AV-002 warns about.
    glCopyImageSubData(gridTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
                       texture_, GL_TEXTURE_2D, 0, 0, static_cast<GLint>(next_), 0,
                       static_cast<GLsizei>(width_), 1, 1);
    next_ = (next_ + 1) % rows_;
    ++captured_;
}

void SpaceTime::draw(Renderer2D& renderer, const Rect& vp, int frameWidth, int frameHeight,
                     unsigned int states, double zoom) const {
    if (texture_ == 0 || zoom <= 0.0) return;
    const core::GridSpec spec{2, width_, rows_, 1, core::CellType::U8};

    // Until the ring has wrapped, the rows are already in order and the
    // unwritten ones below are state 0, so the history visibly fills downward
    // before it starts scrolling.
    if (captured_ < rows_) {
        renderer.drawBand(texture_, spec, vp, frameWidth, frameHeight, states, zoom, 0.0);
        return;
    }

    // Wrapped: the oldest row is the one about to be overwritten. Draw from
    // there to the end of the texture, then the beginning up to the seam.
    const uint32_t topRows = rows_ - next_;
    const float topHeight = static_cast<float>(topRows) * static_cast<float>(zoom);
    const Rect top{vp.x, vp.y, vp.w, std::min(topHeight, vp.h)};
    renderer.drawBand(texture_, spec, top, frameWidth, frameHeight, states, zoom,
                      static_cast<double>(next_));
    if (topHeight < vp.h && next_ > 0) {
        const Rect bottom{vp.x, vp.y + topHeight, vp.w, vp.h - topHeight};
        renderer.drawBand(texture_, spec, bottom, frameWidth, frameHeight, states, zoom, 0.0);
    }
}

}  // namespace aether::render
