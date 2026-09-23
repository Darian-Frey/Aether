#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <raylib.h>

#include <cstdlib>

namespace aether::test {

namespace {

// One context for the whole run, not one per case (BUG-014).
//
// Each [gpu] case used to open and close its own hidden window. Past a few
// dozen of those in one process, GLX stops handing out framebuffer configs —
// `GLX: No GLXFBConfigs returned`, then `Failed to find a suitable
// GLXFBConfig` — and every window after that fails. The cases were written to
// skip when no window comes up, because that is the right answer on a machine
// with no display, so the suite reported success while a growing fraction of
// it had not run: 2 of 49 early in a session and 12 by the end of one.
//
// The window is deliberately never closed. Whether raylib's own state is still
// intact by the time a static destructor runs is not something worth betting
// the suite's exit code on, and the process is ending anyway, so the context
// is left to the operating system to reclaim.
struct SharedContext {
    bool ready = false;

    SharedContext() {
        SetTraceLogLevel(LOG_NONE);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(64, 64, "aether tests");
        ready = IsWindowReady();
    }
};

SharedContext& shared() {
    static SharedContext ctx;
    return ctx;
}

// Whether there is a display to open a window on at all. If there is, a
// failure to get a context is a defect rather than an environment.
bool displayAvailable() {
    const char* x11 = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (x11 != nullptr && *x11 != '\0') || (wayland != nullptr && *wayland != '\0');
}

}  // namespace

GlContext::GlContext() : ready_(shared().ready) {}

GlContext::~GlContext() = default;

void requireGl(const GlContext& ctx) {
    if (ctx.ready()) return;
    if (!displayAvailable()) SKIP("no GL context available (no display)");
    // A display is present and the context still did not come up. Skipping
    // here is what hid BUG-014: it reads as "not applicable here" when it
    // means "this did not run and nobody was told".
    FAIL("a display is present but no GL context could be created");
}

}  // namespace aether::test
