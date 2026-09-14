#include "ui/headless.hpp"

#include "rule/dsl.hpp"
#include "sim/session.hpp"
#include "sim/simulation.hpp"

#include <raylib.h>

#include <cstdio>
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

}  // namespace

int runHeadless(const Options& opts, uint64_t generations, const std::string& savePath) {
    HiddenWindow win;
    if (!win.ready()) return kExitNoContext;
    int code = 0;
    {
        rule::DslContext ctx;
        ctx.dimensions = opts.depth > 1 ? 3 : 2;
        auto parsed = rule::parseDsl(opts.rule, ctx);
        if (!parsed) return fail(std::format("rule: {}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message));
        auto made = sim::Simulation::create(core::GridSpec{ctx.dimensions, opts.width, opts.height, opts.depth}, *parsed.ir,
                                            opts.cpu ? sim::Path::Cpu : sim::Path::Gpu, opts.seed, opts.seedB);
        if (const auto* e = std::get_if<core::Error>(&made)) return fail(e->message);
        auto sim = std::get<sim::Simulation>(std::move(made));
        std::vector<double> density(parsed.ir->states - 1u, 0.1);
        density[0] = parsed.ir->states == 2 ? 0.3 : 0.2;
        sim.fillRandom(density);
        if (opts.ruleMutationInterval > 0) sim.setRuleMutation({true, opts.ruleMutationInterval, opts.ruleMutationMagnitude});
        if (opts.cellMutationP > 0.0) sim.setCellMutation(opts.cellMutationP, static_cast<uint8_t>(opts.cellMutationBlock));
        for (uint64_t g = 0; g < generations; ++g) sim.step();
        if (auto e = sim::saveSession(savePath, sim.session())) code = fail(e->message);
        else std::printf("ran %llu generations, %zu lineage entries, saved %s\n",
                         static_cast<unsigned long long>(generations), sim.lineage().size(), savePath.c_str());
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
    if (diff != 0) return fail(std::format("{} of {} cells differ, first at index {}", diff, sa.current.size(), first));
    if (sa.lineage.size() != sb.lineage.size()) return fail("lineage lengths differ");
    for (size_t i = 0; i < sa.lineage.size(); ++i) {
        if (sa.lineage[i].ir_hash != sb.lineage[i].ir_hash) return fail(std::format("lineage entry {} differs", i));
    }
    std::printf("identical: %zu cells at generation %llu, %zu lineage entries\n",
                sa.current.size(), static_cast<unsigned long long>(sa.generation), sa.lineage.size());
    return 0;
}

}  // namespace aether::ui
