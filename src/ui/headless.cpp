#include "ui/headless.hpp"

#include "render/renderer2d.hpp"
#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "sim/fill.hpp"
#include "sim/session.hpp"
#include "sim/simulation.hpp"
#include "ui/capture.hpp"

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>

namespace aether::ui {

namespace {

class HiddenWindow {
public:
    HiddenWindow() {
        SetTraceLogLevel(LOG_WARNING);
        SetConfigFlags(FLAG_WINDOW_HIDDEN);
        InitWindow(64, 64, "aether");
        ready_ = IsWindowReady();
    }
    ~HiddenWindow() { if (ready_) CloseWindow(); }
    bool ready() const { return ready_; }
private:
    bool ready_ = false;
};

int fail(const std::string& msg) {
    std::fprintf(stderr, "aether: %s\n", msg.c_str());
    return 1;
}

// Renders a grid to PNGs at its own size, through the same palette pass the
// window draws with. Holds GL objects, so it must be scoped inside the hidden
// window's lifetime like everything else that does (BUG-016).
class GridWriter {
public:
    static std::variant<GridWriter, core::Error> create(const core::GridSpec& spec, uint32_t scale) {
        if (spec.dimensions == 3) {
            // A 3D dump would have to say where the camera is, how far the
            // clip planes are and how opaque a cell is — a picture of a volume
            // is a choice in a way a picture of a plane is not. Refused rather
            // than guessed at; the session and the pattern formats carry 3D
            // data losslessly for anything that wants it.
            return core::Error{"a headless image dump is 2D only; a 3D view needs a camera to be specified"};
        }
        auto made = render::Renderer2D::create();
        if (const auto* e = std::get_if<core::Error>(&made)) return *e;

        GridWriter w;
        w.renderer_.emplace(std::move(std::get<render::Renderer2D>(made)));
        w.scale_ = scale < 1 ? 1 : scale;
        w.width_  = static_cast<int>(spec.width * w.scale_);
        w.height_ = static_cast<int>(spec.height * w.scale_);
        w.target_ = LoadRenderTexture(w.width_, w.height_);
        if (w.target_.id == 0) return core::Error{"could not make an offscreen target"};
        w.owns_ = true;
        return w;
    }

    GridWriter(GridWriter&& o) noexcept { *this = std::move(o); }
    GridWriter& operator=(GridWriter&& o) noexcept {
        if (this != &o) {
            release();
            renderer_ = std::move(o.renderer_);
            target_ = o.target_; width_ = o.width_; height_ = o.height_; scale_ = o.scale_;
            owns_ = o.owns_;
            o.owns_ = false;
        }
        return *this;
    }
    GridWriter(const GridWriter&) = delete;
    GridWriter& operator=(const GridWriter&) = delete;
    ~GridWriter() { release(); }

    void setPalette(const render::Palette& p) { renderer_->setPalette(p); }

    bool write(const sim::Simulation& sim, const std::string& path) {
        const core::GridSpec& spec = sim.spec();
        render::View2D view;
        view.zoom = static_cast<double>(scale_);
        view.centre_x = spec.width * 0.5;
        view.centre_y = spec.height * 0.5;
        view.lattice = sim.rule().neighbourhood.type == rule::NeighbourhoodType::Hexagonal
                           ? render::Lattice::Hex : render::Lattice::Square;
        const render::Rect vp{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
        const unsigned int ramp = spec.cell_type == core::CellType::F32 ? 256u : sim.rule().states;

        BeginTextureMode(target_);
        ClearBackground(BLACK);
        renderer_->draw(sim.texture(), spec, view, vp, width_, height_, ramp);
        EndTextureMode();

        Image img = LoadImageFromTexture(target_.texture);
        if (img.data == nullptr) return false;
        // A render texture is bottom-up; a PNG is not.
        ImageFlipVertical(&img);
        const bool ok = ExportImage(img, path.c_str());
        UnloadImage(img);
        return ok;
    }

private:
    GridWriter() = default;
    void release() {
        if (owns_) UnloadRenderTexture(target_);
        owns_ = false;
        renderer_.reset();
    }

    std::optional<render::Renderer2D> renderer_;
    RenderTexture2D target_{};
    int width_ = 0, height_ = 0;
    uint32_t scale_ = 1;
    bool owns_ = false;
};

}  // namespace

int runHeadless(const Options& opts, uint64_t generations, const std::string& savePath,
                const DumpOptions& dump) {
    HiddenWindow win;
    if (!win.ready()) return kExitNoContext;
    int code = 0;
    {
        rule::DslContext ctx;
        ctx.dimensions = opts.dimensions;
        rule::RuleIR ir;
        if (!opts.ruleIsLua && opts.rule.starts_with("@")) {
            // `--rule @name` means the same thing here as it does in the
            // window, and looks in the same places (BUG-017). `@` is not
            // valid in any notation, so there is nothing to disambiguate.
            const std::string wanted = opts.rule.substr(1);
            const auto library = rule::loadLibrary(ruleSearchPath());
            const auto it = std::find_if(library.begin(), library.end(),
                                         [&](const rule::LibraryRule& r) { return r.id == wanted; });
            if (it == library.end()) {
                return fail(std::format("no rule '{}' in the library ({} found)", wanted, library.size()));
            }
            auto built = rule::compileLibraryRule(*it, ctx.boundary);
            if (const auto* e = std::get_if<std::string>(&built)) return fail(wanted + ": " + *e);
            ir = std::get<rule::RuleIR>(std::move(built));
        } else if (opts.ruleIsLua) {
            rule::LuaContext lctx;
            lctx.dimensions = ctx.dimensions;
            auto r = rule::compileLua(opts.rule, lctx);
            if (const auto* e = std::get_if<rule::LuaError>(&r)) return fail("rule: " + e->message);
            ir = std::get<rule::RuleIR>(std::move(r));
        } else {
            auto parsed = rule::parseDsl(opts.rule, ctx);
            if (!parsed) return fail(std::format("rule: {}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message));
            ir = *parsed.ir;
        }

        // The rule decides the dimensionality, as it does in the window: a
        // library rule carries its own, and an elementary rule is 1D whatever
        // `--size` said. Say so rather than silently reshaping the grid.
        uint32_t width = opts.width, height = opts.height, depth = opts.depth;
        if (ir.dimensions != opts.dimensions) {
            if (ir.dimensions < 3) depth = 1;
            if (ir.dimensions < 2) height = 1;
            if (ir.dimensions == 3 && depth == 1) depth = height = width;
            std::printf("rule is %uD; grid is %ux%ux%u\n", ir.dimensions, width, height, depth);
        }
        // The grid's storage follows the rule: a continuous rule needs float
        // cells, and Simulation refuses the pair if they disagree.
        const core::GridSpec spec{ir.dimensions, width, height, depth, ir.cell_type};
        auto made = sim::Simulation::create(spec, ir,
                                            opts.cpu ? sim::Path::Cpu : sim::Path::Gpu, opts.seed, opts.seedB);
        if (const auto* e = std::get_if<core::Error>(&made)) return fail(e->message);
        auto sim = std::get<sim::Simulation>(std::move(made));
        sim.fillRandom(sim::defaultDensity(ir));
        if (opts.ruleMutationInterval > 0) sim.setRuleMutation({true, opts.ruleMutationInterval, opts.ruleMutationMagnitude});
        if (opts.cellMutationP > 0.0) sim.setCellMutation(opts.cellMutationP, static_cast<uint8_t>(opts.cellMutationBlock));
        const bool wantsImages = !dump.png.empty() || !dump.framesDir.empty();
        std::optional<GridWriter> writer;
        if (wantsImages) {
            auto made2 = GridWriter::create(spec, dump.frameScale);
            if (const auto* e = std::get_if<core::Error>(&made2)) return fail(e->message);
            writer.emplace(std::move(std::get<GridWriter>(made2)));
            writer->setPalette(render::Palette::defaultFor(
                spec.cell_type == core::CellType::F32 ? 256 : ir.states, ir.metadata.decay_from));
            if (spec.cell_type == core::CellType::F32) {
                writer->setPalette(render::Palette::continuousRamp());
            }
        }

        // The same `Recording` the window's Export section drives (F-021), so
        // the two agree about which generation is which frame without either
        // knowing the other exists.
        std::optional<Recording> frames;
        if (!dump.framesDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(dump.framesDir, ec);
            if (ec) return fail(std::format("cannot make {}: {}", dump.framesDir, ec.message()));
            Recording r;
            r.from = 0;
            r.to = generations;
            r.every = dump.frameEvery == 0 ? 1 : dump.frameEvery;
            r.dir = dump.framesDir;
            frames = r;
        }

        for (uint64_t g = 0; g <= generations; ++g) {
            if (frames && frames->wants(g)) {
                const std::string path = frames->pathFor(g);
                if (!writer->write(sim, path)) return fail(std::format("cannot write {}", path));
                frames->lastCaptured = g;
                ++frames->written;
            }
            if (g < generations) sim.step();
        }
        if (!dump.png.empty() && !writer->write(sim, dump.png)) {
            return fail(std::format("cannot write {}", dump.png));
        }

        if (!savePath.empty()) {
            if (auto e = sim::saveSession(savePath, sim.session())) code = fail(e->message);
        }
        if (code == 0) {
            std::printf("ran %llu generations, %zu lineage entries", 
                        static_cast<unsigned long long>(generations), sim.lineage().size());
            if (!savePath.empty()) std::printf(", saved %s", savePath.c_str());
            if (frames) std::printf(", %llu frames in %s",
                                    static_cast<unsigned long long>(frames->written), dump.framesDir.c_str());
            if (!dump.png.empty()) std::printf(", wrote %s", dump.png.c_str());
            std::printf("\n");
        }
    }
    return code;
}

int runReplay(const std::string& in, const std::string& out, uint64_t toGeneration, bool cpu) {
    HiddenWindow win;
    if (!win.ready()) return kExitNoContext;
    int code = 0;
    {
        auto loaded = sim::loadSession(in);
        if (const auto* e = std::get_if<sim::SessionError>(&loaded)) return fail(e->message);
        const sim::Session& s = std::get<sim::Session>(loaded);
        const uint64_t target = toGeneration == UINT64_MAX ? s.generation : toGeneration;
        auto made = sim::Simulation::replay(s, {target}, cpu ? sim::Path::Cpu : sim::Path::Gpu);
        if (const auto* e = std::get_if<core::Error>(&made)) return fail(e->message);
        auto sim = std::get<sim::Simulation>(std::move(made));
        if (auto e = sim::saveSession(out, sim.session())) code = fail(e->message);
        else std::printf("replayed %s to generation %llu on the %s path, saved %s\n", in.c_str(),
                         static_cast<unsigned long long>(target), cpu ? "CPU" : "GPU", out.c_str());
    }
    return code;
}

int runCompare(const std::string& a, const std::string& b) {
    auto la = sim::loadSession(a);
    if (const auto* e = std::get_if<sim::SessionError>(&la)) return fail(e->message);
    auto lb = sim::loadSession(b);
    if (const auto* e = std::get_if<sim::SessionError>(&lb)) return fail(e->message);
    const sim::Session& sa = std::get<sim::Session>(la);
    const sim::Session& sb = std::get<sim::Session>(lb);
    if (sa.generation != sb.generation) return fail(std::format("generations differ: {} vs {}", sa.generation, sb.generation));
    if (sa.current.size() != sb.current.size()) return fail("grid sizes differ");
    size_t diff = 0, first = sa.current.size();
    for (size_t i = 0; i < sa.current.size(); ++i) {
        if (sa.current[i] != sb.current[i]) { if (diff == 0) first = i; ++diff; }
    }
    // Counted in bytes, which is the cell count only for u8: an f32 grid is
    // four bytes a cell and would otherwise report four times its size.
    const uint32_t cellBytes = core::cellBytes(sa.spec.cell_type);
    if (diff != 0) {
        return fail(std::format("{} of {} bytes differ, first at cell {}",
                                diff, sa.current.size(), first / cellBytes));
    }
    // Auxiliary fields, on the same terms as the state (F-031). A comparison
    // that only read the state would call two runs identical while the rule's
    // own bookkeeping had diverged, and a field feeds the next generation's
    // state, so that is a difference waiting rather than a difference avoided.
    if (sa.fields.size() != sb.fields.size()) {
        return fail(std::format("field counts differ: {} vs {}", sa.fields.size(), sb.fields.size()));
    }
    for (size_t f = 0; f < sa.fields.size(); ++f) {
        const sim::SessionField& fa = sa.fields[f];
        const sim::SessionField& fb = sb.fields[f];
        if (fa.name != fb.name || fa.cell_type != fb.cell_type) {
            return fail(std::format("field {} is '{}' ({}) and '{}' ({})", f, fa.name,
                                    core::toString(fa.cell_type), fb.name, core::toString(fb.cell_type)));
        }
        if (fa.current.size() != fb.current.size()) {
            return fail(std::format("field '{}' sizes differ", fa.name));
        }
        size_t fdiff = 0, ffirst = fa.current.size();
        for (size_t i = 0; i < fa.current.size(); ++i) {
            if (fa.current[i] != fb.current[i]) { if (fdiff == 0) ffirst = i; ++fdiff; }
        }
        if (fdiff != 0) {
            return fail(std::format("field '{}': {} of {} bytes differ, first at cell {}", fa.name,
                                    fdiff, fa.current.size(), ffirst / core::cellBytes(fa.cell_type)));
        }
    }
    if (sa.lineage.size() != sb.lineage.size()) return fail("lineage lengths differ");
    for (size_t i = 0; i < sa.lineage.size(); ++i) {
        if (sa.lineage[i].ir_hash != sb.lineage[i].ir_hash) return fail(std::format("lineage entry {} differs", i));
    }
    std::printf("identical: %llu %s cells at generation %llu, %zu lineage entries, %zu field(s)\n",
                static_cast<unsigned long long>(sa.spec.cellCount()),
                std::string(core::toString(sa.spec.cell_type)).c_str(),
                static_cast<unsigned long long>(sa.generation), sa.lineage.size(), sa.fields.size());
    return 0;
}

}  // namespace aether::ui
