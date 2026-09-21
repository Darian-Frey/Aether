// Aether entry point.
//
//   aether [--rule R] [--size WxH] [--cpu] [--seed N] [--seed-b N] [--rate G] [--load FILE]
//          [--pattern FILE] [--gl-check]
//   aether headless ... --generations G --save FILE
//   aether replay IN OUT [--to G] [--cpu]
//   aether compare A B
//
// --gl-check opens a hidden window, verifies the GL 4.3 compute path and
// exits 0 or 1; the rest configure the interactive session.

#include "core/gl.hpp"
#include "ui/app.hpp"
#include "ui/headless.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
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
    std::puts("usage: aether [--rule R] [--size WxH] [--cpu] [--seed N] [--seed-b N] [--rate G] [--gl-check]\n"
              "  --rule R     B/S, B/S/C or a table block (default B3/S23); @name loads from the library\n"
              "  --lua FILE   a Lua script returning a rule table, instead of --rule\n"
              "  --size WxH[xD]  grid extents (default 512x512); a depth makes it 3D\n"
              "  --cpu        start on the CPU reference path\n"
              "  --seed N     stream A seed for the random fill (default 1)\n"
              "  --seed-b N   stream B seed for cell mutation (default 2)\n"
              "  --rate G     target generations per second (default 60)\n"
              "  --rule-mutation N[:M]  mutate the rule every N generations with M edits\n"
              "  --cell-mutation P[:K]  mutation probability, optionally in blocks of 2^K cells\n"
              "  --gl-check   verify the compute path and exit\n"
              "  --load FILE  resume a saved session\n"
              "  --pattern F  open a pattern file, ready to place\n"
              "  --frames N   exit after N frames (for scripted runs)\n"
              "  --screenshot F  write the final frame to F before exiting");
}

}  // namespace

int main(int argc, char** argv) {
    aether::ui::Options opts;
    // Subcommands.
    std::string_view sub = argc > 1 ? argv[1] : "";
    uint64_t generations = 0;
    std::string savePath, loadPath;
    uint64_t replayTo = UINT64_MAX;
    int first = 1;
    if (sub == "headless" || sub == "replay" || sub == "compare") first = 2;
    if (sub == "compare") {
        if (argc != 4) { std::puts("usage: aether compare A B"); return 2; }
        return aether::ui::runCompare(argv[2], argv[3]);
    }
    if (sub == "replay") {
        if (argc < 4) { std::puts("usage: aether replay IN OUT [--to G] [--cpu]"); return 2; }
        std::string in = argv[2], out = argv[3];
        bool cpu = false;
        for (int i = 4; i < argc; ++i) {
            const std::string_view a = argv[i];
            if (a == "--cpu") cpu = true;
            else if (a == "--to" && i + 1 < argc) replayTo = std::strtoull(argv[++i], nullptr, 10);
            else { std::printf("unknown option %s\n", argv[i]); return 2; }
        }
        return aether::ui::runReplay(in, out, replayTo, cpu);
    }
    for (int i = first; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) { std::printf("%s needs a value\n", flag); std::exit(2); }
            return argv[++i];
        };
        if (a == "--gl-check") return glCheck();
        if (a == "--help" || a == "-h") { usage(); return 0; }
        if (a == "--rule") { opts.rule = value("--rule"); opts.ruleIsLua = false; }
        else if (a == "--lua") {
            const char* path = value("--lua");
            std::ifstream f(path);
            if (!f) { std::printf("cannot open %s\n", path); return 2; }
            opts.rule.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            opts.ruleIsLua = true;
        }
        else if (a == "--size") {
            unsigned w = 0, h = 0, d = 1;
            const int n = std::sscanf(value("--size"), "%ux%ux%u", &w, &h, &d);
            if (n < 2 || w == 0 || h == 0 || d == 0) {
                std::puts("--size expects WxH or WxHxD"); return 2;
            }
            opts.width = w; opts.height = h; opts.depth = d;
        }
        else if (a == "--cpu") opts.cpu = true;
        else if (a == "--seed") opts.seed = std::strtoull(value("--seed"), nullptr, 10);
        else if (a == "--seed-b") opts.seedB = std::strtoull(value("--seed-b"), nullptr, 10);
        else if (a == "--rate") opts.targetGps = std::strtod(value("--rate"), nullptr);
        else if (a == "--rule-mutation") {
            unsigned n = 0, m = 1;
            if (std::sscanf(value("--rule-mutation"), "%u:%u", &n, &m) < 1 || n == 0) {
                std::puts("--rule-mutation expects INTERVAL or INTERVAL:EDITS"); return 2;
            }
            opts.ruleMutationInterval = n; opts.ruleMutationMagnitude = std::max(1u, m);
        }
        else if (a == "--cell-mutation") {
            const char* v = value("--cell-mutation");
            opts.cellMutationP = std::strtod(v, nullptr);
            if (const char* colon = std::strchr(v, ':')) opts.cellMutationBlock = static_cast<uint32_t>(std::atoi(colon + 1));
        }
        else if (a == "--generations") generations = std::strtoull(value("--generations"), nullptr, 10);
        else if (a == "--save") savePath = value("--save");
        else if (a == "--load") loadPath = value("--load");
        else if (a == "--pattern") opts.pattern = value("--pattern");
        else if (a == "--frames") opts.exitAfterFrames = std::atoi(value("--frames"));
        else if (a == "--screenshot") opts.screenshot = value("--screenshot");
        else { std::printf("unknown option %s\n", argv[i]); usage(); return 2; }
    }
    if (sub == "headless") {
        if (savePath.empty()) { std::puts("headless needs --save FILE"); return 2; }
        return aether::ui::runHeadless(opts, generations, savePath);
    }
    opts.load = loadPath;
    return aether::ui::App(opts).run();
}
