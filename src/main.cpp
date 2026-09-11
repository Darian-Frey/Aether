// Aether — Phase 0 entry point.
//
// Opens a window, confirms that the GL 4.3 compute path D-001 depends on is
// actually available, and shows the result in an ImGui panel. With
// `--gl-check` the same probe runs against a hidden window and the exit code
// reports the outcome, so the check is scriptable.
//
// Nothing here is engine code. It exists to be replaced by Phase 1.

#include <raylib.h>
#include <rlgl.h>
#include "core/gl.hpp"        // glMemoryBarrier; rlgl does not wrap it

#include <imgui.h>
#include <rlImGui.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth  = 960;
constexpr int kWindowHeight = 600;

// A trivially checkable kernel: every element becomes 2n + 1. If the dispatch
// silently does nothing (the 3.3 backend case) the buffer comes back unchanged
// and the check fails loudly rather than looking plausible.
constexpr const char* kProbeShader = R"glsl(
#version 430
layout(local_size_x = 64) in;
layout(std430, binding = 0) buffer Data { uint v[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < v.length()) v[i] = v[i] * 2u + 1u;
}
)glsl";

struct ProbeResult {
    bool        ok = false;
    std::string renderer;
    std::string version;
    std::string detail;
};

ProbeResult probeCompute() {
    ProbeResult r;
    r.renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    r.version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    if (rlGetVersion() != RL_OPENGL_43) {
        r.detail = "rlgl was not built with the 4.3 backend (OPENGL_VERSION)";
        return r;
    }

    constexpr uint32_t kCount = 4096;
    std::vector<uint32_t> data(kCount);
    for (uint32_t i = 0; i < kCount; ++i) data[i] = i;
    const unsigned int bytes = static_cast<unsigned int>(kCount * sizeof(uint32_t));

    const unsigned int shader = rlLoadShader(kProbeShader, RL_COMPUTE_SHADER);
    if (shader == 0) { r.detail = "compute shader failed to compile"; return r; }
    const unsigned int program = rlLoadShaderProgramCompute(shader);
    if (program == 0) { r.detail = "compute program failed to link"; return r; }

    const unsigned int ssbo = rlLoadShaderBuffer(bytes, data.data(), RL_DYNAMIC_COPY);

    rlEnableShader(program);
    rlBindShaderBuffer(ssbo, 0);
    rlComputeShaderDispatch(kCount / 64, 1, 1);
    rlDisableShader();
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

    std::vector<uint32_t> out(kCount, 0);
    rlReadShaderBuffer(ssbo, out.data(), bytes, 0);

    rlUnloadShaderBuffer(ssbo);
    rlUnloadShaderProgram(program);
    rlUnloadShader(shader);

    uint32_t mismatches = 0;
    for (uint32_t i = 0; i < kCount; ++i) {
        if (out[i] != i * 2u + 1u) ++mismatches;
    }
    if (mismatches != 0) {
        r.detail = "dispatch ran but " + std::to_string(mismatches) + " of "
                 + std::to_string(kCount) + " elements were wrong";
        return r;
    }

    r.ok = true;
    r.detail = "compute dispatch of " + std::to_string(kCount) + " elements verified";
    return r;
}

void printResult(const ProbeResult& r) {
    std::printf("renderer : %s\n", r.renderer.c_str());
    std::printf("version  : %s\n", r.version.c_str());
    std::printf("compute  : %s — %s\n", r.ok ? "OK" : "FAILED", r.detail.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    bool checkOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gl-check") == 0) checkOnly = true;
    }

    SetTraceLogLevel(checkOnly ? LOG_WARNING : LOG_INFO);
    if (checkOnly) SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(kWindowWidth, kWindowHeight, "Aether");

    const ProbeResult probe = probeCompute();
    printResult(probe);

    if (checkOnly) {
        CloseWindow();
        return probe.ok ? 0 : 1;
    }

    rlImGuiSetup(true);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{18, 18, 22, 255});

        rlImGuiBegin();
        ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
        ImGui::Begin("Aether — Phase 0");
        ImGui::Text("Renderer: %s", probe.renderer.c_str());
        ImGui::Text("Version:  %s", probe.version.c_str());
        ImGui::Separator();
        if (probe.ok) {
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "GL 4.3 compute: OK");
        } else {
            ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "GL 4.3 compute: FAILED");
        }
        ImGui::TextWrapped("%s", probe.detail.c_str());
        ImGui::Separator();
        ImGui::Text("%d fps", GetFPS());
        ImGui::End();
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
