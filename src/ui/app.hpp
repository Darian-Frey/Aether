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
#include "render/spacetime.hpp"
#include "render/view2d.hpp"
#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "rule/lua.hpp"
#include "sim/pattern.hpp"
#include "sim/pattern_library.hpp"
#include "sim/inspect.hpp"
#include "sim/scratch.hpp"
#include "sim/simulation.hpp"
#include "ui/capture.hpp"
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
    // How many dimensions the grid has. Derived from how many numbers `--size`
    // was given, because a 1D grid and a 2D one of height 1 are different
    // things and the extents alone cannot tell them apart (F-005).
    uint8_t     dimensions = 2;
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
    std::string pattern;               // a pattern file to open, pending placement
};

class App {
public:
    explicit App(Options opts) : opts_(std::move(opts)) {}
    int run();

private:
    // Lifecycle
    bool createSimulation(uint32_t width, uint32_t height, uint32_t depth, const rule::RuleIR& ir, sim::Path path);
    bool is3D() const;
    bool is1D() const;   // a 1D run is shown as a space-time diagram, not as a row (F-005)
    void rebuildSpaceTime();   // sized to the grid and the viewport
    void seedSingleCell();     // one live cell in the middle: how an elementary rule is read
    void layOut();
    static std::string configDirectory();
    render::VolumeSettings volumeSettings() const;
    void drawViewPanel();
    void paintAt3D(const std::array<int, 3>& cell);
    bool adoptSimulation(sim::Simulation&& s, const char* what);   // after load/rewind
    void drawSessionPanel();
    void drawExportPanel();           // PNG and frame sequences (F-021)
    // The viewport as it stands, to a PNG. Must be called inside
    // BeginDrawing and before EndDrawing: after the swap the back buffer is
    // undefined. Called before the panels are drawn, so what is written is
    // the automaton and not the interface around it.
    bool captureViewport(const std::string& path);
    void recordingCapture();          // one frame of a sequence, if this generation is one
    void drawTransportBar();
    void drawViewportOverlay();
    void drawHelpPanel();
    void refreshWindowTitle();
    void drawLibraryPanel();
    void drawPatternsPanel();
    void drawPatternPreview();        // the pending pattern's outline, in ImGui
    void drawPatternPreviewCells();   // its cells, through the grid's own palette pass (IMP-008)
    void drawSelection();             // the selected region, outlined not written
    void savePatternSelection();      // the selected region, out to a file
    void savePatternFile(sim::Pattern p, std::string name, const char* fallbackName);  // the one write to patterns/
    void drawEditor();                // the scratch pad, in a window of its own (F-029)
    void drawEditorGrid();            // the pad's cells, drawn and painted
    void drawInspector();             // what the cell under the cursor is about to do (F-030)
    void drawNeighbourhood(const sim::Inspection&);   // the neighbours, in their own geometry
    bool ensureScratch();             // make one from the live rule if there is none
    std::optional<std::pair<int, int>> cellUnderCursor() const;
    std::optional<std::pair<int, int>> pendingOrigin() const;   // where it would land, in cells
    std::optional<std::string> pendingProblem() const;          // why it would not, or nothing
    void loadLibraryRule(const rule::LibraryRule& entry);
    void applyPaletteOverrides(const rule::RuleIR& ir);
    void saveSessionTo(const std::string& path);
    void loadSessionFrom(const std::string& path);
    void verifyReplay();
    bool compileRuleText();            // ruleText_ -> IR -> sim; reports to log and ruleError_
    std::optional<rule::RuleIR> compileRuleSource();
    rule::RuleIR named(rule::RuleIR ir) const;   // takes the name from the text's header   // the front end the language selector names
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
    // The history of a 1D run. Owned here rather than by the renderer because
    // it is a view of the run over time and is thrown away when the run
    // changes shape (F-005).
    std::optional<render::SpaceTime> spaceTime_;
    int  spaceTimeZoom_ = 2;      // pixels per cell
    render::View2D view_;
    render::Orbit  orbit_;
    std::array<float, 3> clipLo_{0, 0, 0};   // fractions of the grid
    std::array<float, 3> clipHi_{1, 1, 1};
    float opacity_ = 1.0f;
    bool  sliceMode_ = false;
    int   sliceAxis_ = 2;
    int   sliceIndex_ = 0;
    render::Rect   viewport_;
    render::Rect   panelRect_;
    bool           showHelp_ = false;
    std::string    iniPath_;   // must outlive ImGui, which keeps the pointer

    rule::DslContext ctx_;
    std::array<char, 65536> ruleText_{};   // Lua scripts are longer than B/S notation
    int ruleLanguage_ = 0;                 // 0 = DSL, 1 = Lua
    std::string ruleError_;
    std::string ruleSummary_;
    std::string ruleHash_;
    std::string ruleName_;

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
    // A pattern that has been loaded but not yet committed: it follows the
    // cursor and is drawn rather than written, so the grid is untouched until
    // the click (F-012).
    std::optional<sim::Pattern> pending_;
    // The one route to `pending_`, so that the preview texture cannot be left
    // showing the previous pattern. Every assignment bumps `pendingSerial_`,
    // which is what `drawPatternPreview` compares against to decide whether to
    // re-upload (IMP-008).
    void setPending(std::optional<sim::Pattern> p);
    uint64_t pendingSerial_ = 0;
    // The pending pattern as a state texture, so the preview goes through the
    // same palette pass as the grid. A GpuGrid rather than a texture of our
    // own: it already knows how to make and fill one for either cell type.
    std::optional<core::GpuGrid> previewGrid_;
    uint64_t previewSerial_ = 0;
    std::array<char, 512> patternPath_{};
    // A region of the grid, in cells, inclusive of both corners. Shift-drag
    // sets it; it is drawn like the preview and never written to.
    struct Selection { uint32_t x0, y0, x1, y1; };
    std::optional<Selection> selection_;
    std::optional<std::pair<int, int>> selectAnchor_;   // while the drag is live
    // Placing consumes the press, but the button stays down for frames
    // afterwards; without this the same click then paints where it landed.
    bool swallowLeft_ = false;
    std::array<char, 128> savePatternAs_{};
    // The pattern editor's scratch pad (F-029). Outside the session and the
    // journal by design, so it is not part of what a run replays; it holds a
    // rule and a grid of its own and steps on the CPU path whatever the
    // simulation is doing.
    std::optional<sim::Scratch> scratch_;
    bool  showEditor_ = false;
    int   editorWidth_ = 32, editorHeight_ = 32;
    float editorZoom_ = 12.0f;                  // pixels per cell
    std::array<char, 128> editorSaveAs_{};
    // The cell the inspector is reading (F-030). Kept rather than taken from
    // the cursor each frame, so the panel still says something once the mouse
    // has left the pad to go and read it.
    std::optional<std::array<uint32_t, 3>> inspectAt_;
    bool showInspector_ = true;
    // Rebuilt when the pad's rule changes; a StepScratch allocates, and this
    // is on the frame path.
    std::optional<sim::StepScratch> inspectScratch_;
    uint64_t inspectScratchFor_ = 0;   // the ir_hash it was built for
    std::vector<rule::LibraryRule> library_;
    std::vector<sim::LibraryPattern> patternLibrary_;
    std::vector<rule::PaletteOverride> paletteOverrides_;   // from the rule that is loaded
    size_t lastLineageSize_ = 0;
    float cellMutationLog_ = -4.0f;   // log10 of p

    // A frame sequence being written (F-021). While one exists the transport
    // stops deciding how far to step — a recording is specified in
    // generations, and a frame that ran long must not drop or double one.
    std::optional<Recording> recording_;
    std::array<char, 512> exportPath_{};
    std::array<char, 512> recordDir_{};
    int  recordFrom_ = 0, recordTo_ = 500, recordEvery_ = 1;
    bool exportRequested_ = false;    // a single PNG, taken at the right point in the frame

    Log log_;
};

}  // namespace aether::ui
