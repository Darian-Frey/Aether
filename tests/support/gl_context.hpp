// A hidden raylib window for tests that need a GL context.
//
// Tests tagged [gpu] construct one of these and SKIP if no display is
// available, so the suite still passes headless — with those cases reported
// as skipped rather than silently green.

#pragma once

namespace aether::test {

class GlContext {
public:
    GlContext();
    ~GlContext();
    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};

// Calls SKIP() when no context could be made. Use at the top of a [gpu] test.
void requireGl(const GlContext& ctx);

}  // namespace aether::test
