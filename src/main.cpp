// Aether entry point.
//
//   aether [--rule R] [--size WxH] [--cpu] [--seed N] [--rate G] [--gl-check]
//
// --gl-check opens a hidden window, verifies the GL 4.3 compute path and
// exits 0 or 1; the rest configure the interactive session.

#include "core/gl.hpp"
#include "ui/app.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {

int glCheck() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Aether");
    if (!IsWindowReady()) { std::puts("no GL context"); return 1; }
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const bool ok = rlGetVersion() == RL_OPENGL_43;
    std::printf("renderer : %s\nversion  : %s\nrlgl     : %s\n", renderer, version,
                ok ? "OpenGL 4.3 backend" : "NOT the 4.3 backend");
    CloseWindow();
    return ok ? 0 : 1;
}

void usage() {
    std::puts("usage: aether [--rule R] [--size WxH] [--cpu] [--seed N] [--rate G] [--gl-check]\n"
              "  --rule R     B/S, B/S/C or a table block (default B3/S23)\n"
              "  --size WxH   grid extents (default 512x512)\n"
              "  --cpu        start on the CPU reference path\n"
              "  --seed N     stream A seed for the random fill (default 1)\n"
              "  --rate G     target generations per second (default 60)\n"
              "  --gl-check   verify the compute path and exit\n"
              "  --frames N   exit after N frames (for scripted runs)\n"
              "  --screenshot F  write the final frame to F before exiting");
}

}  // namespace

int main(int argc, char** argv) {
    aether::ui::Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::printf("%s needs a value\n", flag); std::exit(2); }
            return argv[++i];
        };
        if (a == "--gl-check") return glCheck();
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (a == "--rule") opts.rule = value("--rule");
        else if (a == "--size") {
            unsigned w = 0, h = 0;
            if (std::sscanf(value("--size"), "%ux%u", &w, &h) != 2 || w == 0 || h == 0) {
                std::puts("--size expects WxH"); return 2;
            }
            opts.width = w; opts.height = h;
        }
        else if (a == "--cpu") opts.cpu = true;
        else if (a == "--seed") opts.seed = std::strtoull(value("--seed"), nullptr, 10);
        else if (a == "--rate") opts.targetGps = std::strtod(value("--rate"), nullptr);
        else if (a == "--frames") opts.exitAfterFrames = std::atoi(value("--frames"));
        else if (a == "--screenshot") opts.screenshot = value("--screenshot");
        else { std::printf("unknown option %s\n", argv[i]); usage(); return 2; }
    }
    return aether::ui::App(opts).run();
}
