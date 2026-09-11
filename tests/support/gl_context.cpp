#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <raylib.h>

namespace aether::test {

GlContext::GlContext() {
    SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "aether tests");
    ready_ = IsWindowReady();
}

GlContext::~GlContext() {
    if (ready_) CloseWindow();
}

void requireGl(const GlContext& ctx) {
    if (!ctx.ready()) SKIP("no GL context available (no display?)");
}

}  // namespace aether::test
