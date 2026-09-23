// A hidden raylib window for tests that need a GL context.
//
// Tests tagged [gpu] construct one of these and SKIP if no display is
// available, so the suite still passes headless — with those cases reported
// as skipped rather than silently green.
//
// One context serves the whole run rather than one per case (BUG-014): GLX
// runs out of framebuffer configs after a few dozen init/teardown cycles, and
// because the cases skip when no window comes up, the suite reported success
// while a growing fraction of it quietly did not run. Constructing a
// GlContext is therefore cheap and does not create anything; it just answers
// whether the shared context came up.
//
// Sharing it means raylib's global state persists between cases. Nothing in
// the suite depends on a fresh one, but a case that changes a global — a blend
// mode, a bound render texture — should put it back.

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
