// The bundled pattern library (F-027). The counterpart of the rule-library
// test: every file in `patterns/` must parse, and must fit the rule it names.

#include "rule/library.hpp"
#include "rule/lua.hpp"
#include <cstdlib>
#include <optional>
#include "rule/dsl.hpp"
#include "sim/pattern_library.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace aether;

namespace {

std::vector<std::string> searchPath() {
    const char* env = std::getenv("AETHER_PATTERNS");
    return {env ? env : "", "patterns", "../patterns", "../../patterns"};
}

std::vector<std::string> ruleSearchPath() {
    const char* env = std::getenv("AETHER_RULES");
    return {env ? env : "", "rules", "../rules", "../../rules"};
}

}  // namespace

TEST_CASE("every bundled pattern parses", "[pattern][library]") {
    const auto library = sim::loadPatternLibrary(searchPath());
    if (library.empty()) SKIP("no patterns/ directory beside the test binary");

    // loadPatternLibrary skips a file it cannot read, so a silent skip would
    // look exactly like a library that never had the file. Count the files.
    namespace fs = std::filesystem;
    size_t onDisk = 0;
    for (const std::string& dir : searchPath()) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".rle" || e.path().extension() == ".pattern") ++onDisk;
        }
        if (onDisk > 0) break;
    }
    INFO("a pattern that will not parse is skipped rather than reported");
    CHECK(library.size() == onDisk);

    for (const sim::LibraryPattern& p : library) {
        INFO(p.path);
        CHECK(p.pattern.problems().empty());
        CHECK_FALSE(p.name.empty());
        CHECK_FALSE(p.description.empty());     // provenance lives here
        CHECK(p.pattern.cellCount() > 0);
    }
}

TEST_CASE("every bundled pattern fits the rule it names", "[pattern][library]") {
    const auto patterns = sim::loadPatternLibrary(searchPath());
    if (patterns.empty()) SKIP("no patterns/ directory beside the test binary");
    const auto rules = rule::loadLibrary(ruleSearchPath());
    if (rules.empty()) SKIP("no rules/ directory beside the test binary");

    for (const sim::LibraryPattern& p : patterns) {
        INFO(p.path);
        REQUIRE(p.pattern.rule.has_value());     // a bundled pattern says what it is for

        // The name in the header is either a rule in the library or a
        // notation the DSL understands; either way it has to exist, or the
        // pattern is for a rule nobody can load.
        // Compiled the way the interface compiles a library rule: Lua through
        // the Lua front end, and the header's dimensions handed to the DSL,
        // which reads that line as a comment and would otherwise call a 3D
        // rule two-dimensional.
        std::optional<rule::RuleIR> ir;
        for (const rule::LibraryRule& r : rules) {
            if (r.name != *p.pattern.rule && r.id != *p.pattern.rule) continue;
            if (r.isLua) {
                rule::LuaContext lctx;
                lctx.dimensions = r.dimensions;
                auto made = rule::compileLua(r.source, lctx);
                if (const auto* ir2 = std::get_if<rule::RuleIR>(&made)) ir = *ir2;
            } else {
                rule::DslContext ctx;
                ctx.dimensions = r.dimensions;
                auto parsed = rule::parseDsl(r.source.c_str(), ctx);
                if (parsed) ir = *parsed.ir;
            }
            break;
        }
        if (!ir) {
            auto parsed = rule::parseDsl(p.pattern.rule->c_str());
            INFO("rule '" << *p.pattern.rule << "' is neither in the library nor valid notation");
            REQUIRE(parsed);
            ir = *parsed.ir;
        }

        // Fits: the same lattice, and no state the rule has not got.
        const auto lattice = ir->neighbourhood.type == rule::NeighbourhoodType::Hexagonal
                                 ? sim::Lattice::Hexagonal : sim::Lattice::Square;
        CHECK(p.pattern.lattice == lattice);
        CHECK(p.pattern.dimensions == ir->dimensions);
        CHECK(p.pattern.states <= ir->states);
    }
}
