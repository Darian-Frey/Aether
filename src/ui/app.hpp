// The application: window, simulation, renderer, panels and canvas.
//
// Owns everything with a GL lifetime inside run(), so it is all destroyed
// before the window closes. The canvas is the one place user input mutates
// grid state, and it does so through Simulation::paintSpan, never by
// touching a texture (ARCHITECTURE §ui/).

#pragma once

#include "render/orbit.hpp"
#include "render/renderer2d.hpp"
#include "render/renderer3d.hpp"
#include "render/view2d.hpp"
#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "rule/lua.hpp"
#include "sim/simulation.hpp"
#include "ui/log.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aether::ui {

struct Options {
    std::string rule   = "B3/S23";
    bool        ruleIsLua = false;
    uint32_t    width  = 512;
    uint32_t    height = 512;
    uint32_t    depth  = 1;      // > 1 makes a 3D grid
    bool        cpu    = false;
    uint64_t    seed   = 1;      // stream A
    uint64_t    seedB  = 2;      // stream B
    double      targetGps = 60.0;
    uint32_t    ruleMutationInterval = 0;   // 0 = off
    uint32_t    ruleMutationMagnitude = 1;
    double      cellMutationP = 0.0;        // 0 = off
    uint32_t    cellMutationBlock = 0;      // block shift
    int         windowWidth  = 1280;
    int         windowHeight = 800;
    int         exitAfterFrames = 0;   // > 0: run this many frames, then exit
    std::string screenshot;            // if set, written just before exiting
    std::string load;                  // session to resume instead of starting fresh
};

class App {
public:
    explicit App(Options opts) : opts_(std::move(opts)) {}
    int run();

private:
    // Lifecycle
    bool createSimulation(uint32_t width, uint32_t height, uint32_t depth, const rule::RuleIR& ir, sim::Path path);
    bool is3D() const;
    render::VolumeSettings volumeSettings() const;
    void drawViewPanel();
    void paintAt3D(const std::array<int, 3>& cell);
    bool adoptSimulation(sim::Simulation&& s, const char* what);   // after load/rewind
    void drawSessionPanel();
    void drawLibraryPanel();
    void loadLibraryRule(const rule::LibraryRule& entry);
    void applyPaletteOverrides(const rule::RuleIR& ir);
    void saveSessionTo(const std::string& path);
    void loadSessionFrom(const std::string& path);
    void verifyReplay();
    bool compileRuleText();            // ruleText_ -> IR -> sim; reports to log and ruleError_
    std::optional<rule::RuleIR> compileRuleSource();   // the front end the language selector names
    void applyPaletteForStates();
    void refreshRuleSummary();

    // Per frame
    void updateCanvas(double dt);
    void drawPanels();
    void drawRulePanel();
    void drawSimulationPanel();
    void drawGridPanel();
    void drawBrushPanel();
    void drawMutationPanel();
    void drawLineagePanel();
    void drawPalettePanel();
    void drawLogPanel();

    void paintAt(int cx, int cy);
    void fitView();

    Options opts_;

    std::optional<sim::Simulation>    sim_;
    std::optional<render::Renderer2D> renderer_;
    std::optional<render::Renderer3D> renderer3d_;
    render::View2D view_;
    render::Orbit  orbit_;
    std::array<float, 3> clipLo_{0, 0, 0};   // fractions of the grid
    std::array<float, 3> clipHi_{1, 1, 1};
    float opacity_ = 1.0f;
    bool  sliceMode_ = false;
    int   sliceAxis_ = 2;
    int   sliceIndex_ = 0;
    render::Rect   viewport_;

    rule::DslContext ctx_;
    std::array<char, 65536> ruleText_{};   // Lua scripts are longer than B/S notation
    int ruleLanguage_ = 0;                 // 0 = DSL, 1 = Lua
    std::string ruleError_;
    std::string ruleSummary_;

    struct { uint8_t state = 1; int radius = 1; } brush_;
    std::optional<std::pair<int, int>> lastPaintCell_;
    bool panning_ = false;

    std::vector<float> density_;
    int newWidth_ = 512, newHeight_ = 512, newDepth_ = 1;
    int burstCount_ = 1000;
    float targetGpsLog_ = 0.0f;   // log10 of the target, for the slider
    bool  cellMutationOn_ = false;
    int   cellMutationBlock_ = 0;   // shift: 0 = per cell
    bool  ruleMutationOn_ = false;
    int   ruleInterval_ = 250;
    int   ruleMagnitude_ = 1;
    std::array<char, 64> pinName_{};
    std::array<char, 512> sessionPath_{};
    std::array<char, 64>  saveRuleId_{};
    std::vector<rule::LibraryRule> library_;
    std::vector<rule::PaletteOverride> paletteOverrides_;   // from the rule that is loaded
    size_t lastLineageSize_ = 0;
    float cellMutationLog_ = -4.0f;   // log10 of p

    Log log_;
};

}  // namespace aether::ui
