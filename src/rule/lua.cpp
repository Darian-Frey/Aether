#include "rule/lua.hpp"

#include "rule/growth.hpp"

#include "rule/table_layout.hpp"

#include <lua.hpp>

#include <cstdlib>
#include <optional>
#include <span>
#include <cstring>
#include <format>
#include <vector>

namespace aether::rule {

namespace {

// Hook granularity. The abort point is a multiple of this and so is the same
// on every machine, which is the whole reason the budget counts instructions
// rather than seconds (AV-009).
constexpr uint64_t kHookStep = 1'000'000;

// Everything the sandbox needs to know, reachable from anywhere in the state
// through lua_getallocf.
struct Sandbox {
    uint64_t instructions = 0;
    uint64_t instructionBudget = 0;
    size_t   used = 0;
    size_t   memoryBudget = 0;
    bool     outOfMemory = false;
    bool     outOfInstructions = false;
};

Sandbox& sandboxOf(lua_State* L) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    return *static_cast<Sandbox*>(ud);
}

void* boundedAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* box = static_cast<Sandbox*>(ud);
    const size_t old = ptr != nullptr ? osize : 0;
    if (nsize == 0) {
        std::free(ptr);
        box->used -= old;
        return nullptr;
    }
    if (nsize > old && box->used + (nsize - old) > box->memoryBudget) {
        box->outOfMemory = true;
        return nullptr;   // Lua turns this into a memory error
    }
    void* fresh = std::realloc(ptr, nsize);
    if (fresh != nullptr) box->used = box->used - old + nsize;
    return fresh;
}

void countHook(lua_State* L, lua_Debug*) {
    Sandbox& box = sandboxOf(L);
    box.instructions += kHookStep;
    if (box.instructions > box.instructionBudget) {
        box.outOfInstructions = true;
        luaL_error(L, "instruction budget of %I exceeded", static_cast<lua_Integer>(box.instructionBudget));
    }
}

// Closes the interpreter however the call leaves: the isolation in D-003 is
// this destructor, not a convention.
class State {
public:
    explicit State(Sandbox& box) : L_(lua_newstate(boundedAlloc, &box)) {}
    ~State() { if (L_ != nullptr) lua_close(L_); }
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    lua_State* get() const { return L_; }
    explicit operator bool() const { return L_ != nullptr; }

private:
    lua_State* L_;
};

// SPEC §8: exactly these names, in a table of their own. The real global
// table is never the chunk's environment, so `io` and friends are not merely
// removed — they were never reachable.
const char* const kAllowed[] = {"math", "string", "table", "ipairs", "pairs", "select",
                                "tonumber", "tostring", "type", "error", "assert"};

void buildEnvironment(lua_State* L) {
    lua_newtable(L);                                   // env
    for (const char* name : kAllowed) {
        lua_getglobal(L, name);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }
        lua_setfield(L, -2, name);
    }
}

// --- reading the returned table ---------------------------------------------

std::string typeName(lua_State* L, int index) { return luaL_typename(L, index); }

std::optional<lua_Integer> integerField(lua_State* L, int table, const char* name, std::string& err) {
    lua_getfield(L, table, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return std::nullopt; }
    if (!lua_isinteger(L, -1)) {
        err = std::format("field '{}' must be an integer, not a {}", name, typeName(L, -1));
        lua_pop(L, 1);
        return std::nullopt;
    }
    const lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

std::optional<std::string> stringField(lua_State* L, int table, const char* name, std::string& err) {
    lua_getfield(L, table, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return std::nullopt; }
    if (lua_type(L, -1) != LUA_TSTRING) {
        err = std::format("field '{}' must be a string, not a {}", name, typeName(L, -1));
        lua_pop(L, 1);
        return std::nullopt;
    }
    std::string v = lua_tostring(L, -1);
    lua_pop(L, 1);
    return v;
}

std::optional<double> numberField(lua_State* L, int table, const char* name, std::string& err) {
    lua_getfield(L, table, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return std::nullopt; }
    if (!lua_isnumber(L, -1)) {
        err = std::format("field '{}' must be a number, not a {}", name, typeName(L, -1));
        lua_pop(L, 1);
        return std::nullopt;
    }
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

// A continuous rule's kernel and growth function (F-006, Phase 5). The profile
// is a plain list of numbers, which is what makes this cheap: the script has
// math.* and works the shell out at compile time, so nothing here has to know
// what a Gaussian is. The growth function is named rather than written out,
// because Lenia configurations are published as a form and two numbers.
bool readKernel(lua_State* L, int rule, RuleIR& ir, std::string& err) {
    lua_getfield(L, rule, "kernel");
    if (!lua_istable(L, -1)) {
        err = std::format("a continuous rule needs a 'kernel' table of {{shape, profile}}, not a {}",
                          typeName(L, -1));
        return false;
    }
    const int kt = lua_gettop(L);
    Kernel k;
    if (const auto shape = stringField(L, kt, "shape", err)) {
        if (*shape == "radial")        k.shape = Kernel::Shape::Radial;
        else if (*shape == "explicit") k.shape = Kernel::Shape::Explicit;
        else { err = std::format("unknown kernel shape '{}'; the shapes are radial and explicit", *shape); return false; }
    }
    if (!err.empty()) return false;

    lua_getfield(L, kt, "profile");
    if (!lua_istable(L, -1)) {
        err = std::format("kernel needs a 'profile' list of weights, not a {}", typeName(L, -1));
        return false;
    }
    const lua_Unsigned len = lua_rawlen(L, -1);
    k.profile.reserve(len);
    for (lua_Unsigned i = 1; i <= len; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        if (!lua_isnumber(L, -1)) {
            err = std::format("kernel profile[{}] is a {}, not a number", i, typeName(L, -1));
            return false;
        }
        k.profile.push_back(static_cast<float>(lua_tonumber(L, -1)));
        lua_pop(L, 1);
    }
    lua_pop(L, 2);   // profile, kernel

    lua_getfield(L, rule, "growth");
    if (!lua_istable(L, -1)) {
        err = std::format("a continuous rule needs a 'growth' table of {{form, mu, sigma}}, not a {}",
                          typeName(L, -1));
        return false;
    }
    const int gt = lua_gettop(L);
    GrowthSpec g;
    const auto form = stringField(L, gt, "form", err);
    if (!err.empty()) return false;
    if (!form) { err = "growth needs a 'form'"; return false; }
    const auto parsedForm = parseGrowthForm(*form);
    if (!parsedForm) {
        err = std::format("unknown growth form '{}'; the forms are rectangular and polynomial", *form);
        return false;
    }
    g.form = *parsedForm;
    const auto mu = numberField(L, gt, "mu", err);
    if (!err.empty()) return false;
    if (!mu) { err = "growth needs a 'mu', the value the convolution grows at"; return false; }
    g.mu = static_cast<float>(*mu);
    const auto sigma = numberField(L, gt, "sigma", err);
    if (!err.empty()) return false;
    if (!sigma) { err = "growth needs a 'sigma', the width of the band it grows in"; return false; }
    g.sigma = static_cast<float>(*sigma);
    const auto dt = numberField(L, gt, "dt", err);
    if (!err.empty()) return false;
    if (dt) g.dt = static_cast<float>(*dt);
    lua_pop(L, 1);

    if (const auto bad = problems(g); !bad.empty()) { err = bad.front(); return false; }
    k.growth = growthExpression(g);
    ir.transition = std::move(k);
    return true;
}

void readMetadata(lua_State* L, int rule, RuleIR& ir) {
    lua_getfield(L, rule, "metadata");
    if (lua_istable(L, -1)) {
        const int meta = lua_gettop(L);
        std::string ignore;
        if (auto v = stringField(L, meta, "name", ignore)) ir.metadata.name = *v;
        if (auto v = stringField(L, meta, "author", ignore)) ir.metadata.author = *v;
    }
    lua_pop(L, 1);
}

// Calls the script's transition function for one entry and reads the state
// it returns. `describe` names the arguments if something is wrong.
std::optional<uint8_t> callTransition(lua_State* L, int fn, int args, uint16_t states,
                                      const std::string& describe, std::string& err) {
    if (lua_pcall(L, args, 1, 0) != LUA_OK) {
        err = std::format("transition function failed at {}: {}", describe, lua_tostring(L, -1));
        lua_pop(L, 1);
        return std::nullopt;
    }
    (void)fn;
    if (!lua_isinteger(L, -1)) {
        err = std::format("transition function returned a {} at {}; it must return a state",
                          typeName(L, -1), describe);
        lua_pop(L, 1);
        return std::nullopt;
    }
    const lua_Integer v = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (v < 0 || v >= states) {
        err = std::format("transition function returned {} at {}; states run 0 to {}", v, describe, states - 1);
        return std::nullopt;
    }
    return static_cast<uint8_t>(v);
}

void pushIndexedArray(lua_State* L, const std::vector<uint32_t>& values, bool includeZero) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    for (size_t i = 0; i < values.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(values[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(includeZero ? i : i + 1));
    }
}

std::string describeCounts(uint8_t own, const std::vector<uint32_t>& counts) {
    std::string s = std::format("own {} with counts [", own);
    for (size_t i = 1; i < counts.size(); ++i) s += std::format("{}{}", i > 1 ? ", " : "", counts[i]);
    return s + "]";
}

// Builds the transition table by calling the script's function once per
// entry, or by reading the array it returned.
bool buildTable(lua_State* L, int transitionIndex, const RuleIR& shape, const TableLayout& layout,
                uint32_t N, Table& out, std::string& err) {
    const uint16_t S = shape.states;
    out.entries.assign(*layout.size(), 0);

    if (lua_istable(L, transitionIndex)) {
        const lua_Unsigned len = lua_rawlen(L, transitionIndex);
        if (len != out.entries.size()) {
            err = std::format("transition array has {} entries; kind {} with {} states and {} neighbours needs {}",
                              len, toString(shape.kind), S, N, out.entries.size());
            return false;
        }
        for (lua_Unsigned i = 0; i < len; ++i) {
            lua_rawgeti(L, transitionIndex, static_cast<lua_Integer>(i + 1));
            if (!lua_isinteger(L, -1)) {
                err = std::format("transition[{}] is a {}, not a state", i + 1, typeName(L, -1));
                lua_pop(L, 1);
                return false;
            }
            const lua_Integer v = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (v < 0 || v >= S) {
                err = std::format("transition[{}] is {}; states run 0 to {}", i + 1, v, S - 1);
                return false;
            }
            out.entries[i] = static_cast<uint8_t>(v);
        }
        return true;
    }

    if (!lua_isfunction(L, transitionIndex)) {
        err = std::format("field 'transition' must be an array or a function, not a {}",
                          typeName(L, transitionIndex));
        return false;
    }

    switch (shape.kind) {
        case Kind::OuterTotalistic: {
            std::vector<uint32_t> counts(S, 0);   // index 0 carries the quiescent count
            bool ok = true;
            layout.forEachCountVector([&](std::span<const uint32_t> vector) {
                if (!ok) return;
                uint32_t nonZero = 0;
                for (uint16_t i = 0; i + 1u < S; ++i) { counts[i + 1u] = vector[i]; nonZero += vector[i]; }
                counts[0] = N - nonZero;
                for (uint16_t own = 0; own < S && ok; ++own) {
                    lua_pushvalue(L, transitionIndex);
                    lua_pushinteger(L, own);
                    pushIndexedArray(L, counts, true);
                    const auto v = callTransition(L, transitionIndex, 2, S, describeCounts(static_cast<uint8_t>(own), counts), err);
                    if (!v) { ok = false; return; }
                    out.entries[layout.indexOuterTotalistic(static_cast<uint8_t>(own), vector)] = *v;
                }
            });
            return ok;
        }
        case Kind::NonTotalistic: {
            uint64_t signatures = 1;
            for (uint32_t i = 0; i < N; ++i) signatures *= S;
            std::vector<uint8_t>  nbr(N);
            std::vector<uint32_t> asInts(N);
            for (uint64_t sig = 0; sig < signatures; ++sig) {
                uint64_t rest = sig;
                for (uint32_t i = 0; i < N; ++i) { nbr[i] = static_cast<uint8_t>(rest % S); asInts[i] = nbr[i]; rest /= S; }
                for (uint16_t own = 0; own < S; ++own) {
                    lua_pushvalue(L, transitionIndex);
                    lua_pushinteger(L, own);
                    pushIndexedArray(L, asInts, false);
                    const auto v = callTransition(L, transitionIndex, 2, S,
                                                  std::format("own {} with signature {}", own, sig), err);
                    if (!v) return false;
                    out.entries[layout.indexNonTotalistic(static_cast<uint8_t>(own), nbr)] = *v;
                }
            }
            return true;
        }
        case Kind::CountedTotalistic: {
            // f(own, k): how many neighbours fall in this state's set.
            for (uint16_t own = 0; own < S; ++own) {
                for (uint32_t k = 0; k <= N; ++k) {
                    lua_pushvalue(L, transitionIndex);
                    lua_pushinteger(L, own);
                    lua_pushinteger(L, k);
                    const auto v = callTransition(L, transitionIndex, 2, S,
                                                  std::format("own {} with {} counted neighbours", own, k), err);
                    if (!v) return false;
                    out.entries[layout.indexCounted(static_cast<uint8_t>(own), k)] = *v;
                }
            }
            return true;
        }
        case Kind::Totalistic: {
            for (uint32_t sum = 0; sum < out.entries.size(); ++sum) {
                lua_pushvalue(L, transitionIndex);
                lua_pushinteger(L, sum);
                const auto v = callTransition(L, transitionIndex, 1, S, std::format("sum {}", sum), err);
                if (!v) return false;
                out.entries[sum] = *v;
            }
            return true;
        }
        default:
            err = std::format("kind {} cannot be built from a function", toString(shape.kind));
            return false;
    }
}

}  // namespace

std::variant<RuleIR, LuaError> compileLua(std::string_view source, const LuaContext& ctx) {
    Sandbox box;
    box.instructionBudget = ctx.instructionBudget;
    box.memoryBudget = ctx.memoryBudget;

    State state(box);
    if (!state) return LuaError{"could not create a Lua interpreter"};
    lua_State* L = state.get();

    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
    };
    for (const luaL_Reg& lib : libs) {
        luaL_requiref(L, lib.name, lib.func, 1);
        lua_pop(L, 1);
    }
    lua_sethook(L, countHook, LUA_MASKCOUNT, static_cast<int>(kHookStep));

    if (luaL_loadbuffer(L, source.data(), source.size(), "=rule") != LUA_OK) {
        return LuaError{std::format("syntax error: {}", lua_tostring(L, -1))};
    }
    buildEnvironment(L);
    lua_setupvalue(L, -2, 1);   // the chunk's _ENV

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        std::string message = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        if (box.outOfInstructions) {
            message = std::format("script exceeded the instruction budget of {}", ctx.instructionBudget);
        } else if (box.outOfMemory) {
            message = std::format("script exceeded the memory budget of {} MB", ctx.memoryBudget / (1024 * 1024));
        }
        return LuaError{message};
    }
    if (!lua_istable(L, -1)) {
        return LuaError{std::format("the script must return a table describing the rule, not a {}",
                                    typeName(L, -1))};
    }
    const int rule = lua_gettop(L);

    RuleIR ir;
    std::string err;
    ir.dimensions = static_cast<uint8_t>(integerField(L, rule, "dimensions", err).value_or(ctx.dimensions));
    if (!err.empty()) return LuaError{err};
    if (ir.dimensions < 1 || ir.dimensions > 3) {
        return LuaError{std::format("dimensions must be 1, 2 or 3 (got {})", ir.dimensions)};
    }

    // Read before the state count, because it decides whether one means
    // anything: an f32 cell holds a value, not an index into a state set.
    if (const auto ct = stringField(L, rule, "cell_type", err)) {
        const auto parsed = core::parseCellType(*ct);
        if (!parsed) return LuaError{std::format("unknown cell_type '{}'", *ct)};
        ir.cell_type = *parsed;
    }
    if (!err.empty()) return LuaError{err};
    const bool continuous = ir.cell_type == core::CellType::F32;

    const auto states = integerField(L, rule, "states", err);
    if (!err.empty()) return LuaError{err};
    if (continuous) {
        if (states) return LuaError{"a continuous rule has no 'states': an f32 cell holds a value, not an index"};
    } else {
        if (!states) return LuaError{"the rule needs a 'states' count"};
        if (*states < 2 || *states > 256) return LuaError{std::format("states must be in 2..256 (got {})", *states)};
        ir.states = static_cast<uint16_t>(*states);
    }

    lua_getfield(L, rule, "neighbourhood");
    if (!lua_istable(L, -1)) return LuaError{"the rule needs a 'neighbourhood' table of {type, radius}"};
    const int nbTable = lua_gettop(L);
    const auto nbType = stringField(L, nbTable, "type", err);
    if (!err.empty()) return LuaError{err};
    if (!nbType) return LuaError{"neighbourhood needs a 'type'"};
    const auto parsedType = parseNeighbourhoodType(*nbType);
    if (!parsedType) return LuaError{std::format("unknown neighbourhood type '{}'", *nbType)};
    ir.neighbourhood.type = *parsedType;
    const auto radius = integerField(L, nbTable, "radius", err);
    if (!err.empty()) return LuaError{err};
    if (!radius || *radius < 1 || *radius > 127) return LuaError{"neighbourhood needs a radius of at least 1"};
    ir.neighbourhood.radius = static_cast<uint8_t>(*radius);
    lua_pop(L, 1);

    ir.boundary = ctx.boundary;
    if (const auto boundary = stringField(L, rule, "boundary", err)) {
        const auto parsed = parseBoundary(*boundary);
        if (!parsed) return LuaError{std::format("unknown boundary '{}'", *boundary)};
        ir.boundary = *parsed;
    }
    if (!err.empty()) return LuaError{err};

    const auto kind = stringField(L, rule, "kind", err);
    if (!err.empty()) return LuaError{err};
    ir.kind = continuous ? Kind::Continuous : Kind::OuterTotalistic;
    if (kind) {
        const auto parsed = parseKind(*kind);
        if (!parsed) return LuaError{std::format("unknown kind '{}'", *kind)};
        ir.kind = *parsed;
    }
    if (ir.kind == Kind::Expression) {
        return LuaError{"kind expression cannot be returned yet: there is no way to write one here"};
    }

    // Caught here rather than left to validate(), so that a discrete rule
    // claiming the continuous kind is told what is actually wrong instead of
    // being asked for a kernel it was never going to have.
    if ((ir.kind == Kind::Continuous) != continuous) {
        return LuaError{std::format("kind {} and cell_type {} do not agree: the continuous kind is the f32 one",
                                    toString(ir.kind), core::toString(ir.cell_type))};
    }

    // A continuous rule has a kernel where a discrete one has a table, so the
    // rest of the discrete path does not apply to it.
    if (ir.kind == Kind::Continuous) {
        if (!readKernel(L, rule, ir, err)) return LuaError{err};
        readMetadata(L, rule, ir);
        if (const auto diagnostics = validate(ir); !diagnostics.empty()) {
            return LuaError{"the returned rule is not valid: " + diagnostics.front().message};
        }
        return ir;
    }

    // A counted rule must say what each state counts: the transition is a
    // function, so nothing can be inferred from it (D-016).
    if (ir.kind == Kind::CountedTotalistic) {
        lua_getfield(L, rule, "counted");
        const int counted = lua_gettop(L);
        if (lua_isnil(L, counted)) {
            return LuaError{"counted_totalistic needs a 'counted' field: a list of states, or a "
                            "function taking an own state and returning one"};
        }
        for (uint16_t own = 0; own < ir.states; ++own) {
            if (lua_isfunction(L, counted)) {
                lua_pushvalue(L, counted);
                lua_pushinteger(L, own);
                if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
                    return LuaError{std::format("counted function failed for state {}: {}", own, lua_tostring(L, -1))};
                }
            } else if (lua_istable(L, counted)) {
                lua_pushvalue(L, counted);
            } else {
                return LuaError{std::format("field 'counted' must be a list or a function, not a {}",
                                            typeName(L, counted))};
            }
            if (!lua_istable(L, -1)) {
                return LuaError{std::format("counted for state {} is a {}; it must be a list of states",
                                            own, typeName(L, -1))};
            }
            StateSet set;
            const lua_Unsigned len = lua_rawlen(L, -1);
            for (lua_Unsigned i = 1; i <= len; ++i) {
                lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
                if (!lua_isinteger(L, -1)) {
                    return LuaError{std::format("counted for state {} holds a {}; it must be a list of states",
                                                own, typeName(L, -1))};
                }
                const lua_Integer v = lua_tointeger(L, -1);
                lua_pop(L, 1);
                if (v < 0 || v >= ir.states) {
                    return LuaError{std::format("counted for state {} names state {}; states run 0 to {}",
                                                own, v, ir.states - 1)};
                }
                set.set(static_cast<uint16_t>(v));
            }
            lua_pop(L, 1);
            ir.counted.push_back(set);
        }
        lua_pop(L, 1);
    }

    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const auto size = tableSize(ir.kind, ir.states, N);
    if (!size || *size > kLutMaxEntries) {
        return LuaError{std::format("kind {} with {} states and {} neighbours needs {} table entries, "
                                    "against a limit of {}",
                                    toString(ir.kind), ir.states, N,
                                    size ? std::to_string(*size) : "more than 2^64", kLutMaxEntries)};
    }
    const TableLayout layout(ir.kind, ir.states, N);

    lua_getfield(L, rule, "transition");
    if (lua_isnil(L, -1)) return LuaError{"the rule needs a 'transition'"};
    Table table;
    if (!buildTable(L, lua_gettop(L), ir, layout, N, table, err)) return LuaError{err};
    lua_pop(L, 1);
    ir.transition = std::move(table);

    readMetadata(L, rule, ir);

    if (const auto diagnostics = validate(ir); !diagnostics.empty()) {
        return LuaError{"the returned rule is not valid: " + diagnostics.front().message};
    }
    return ir;
}

}  // namespace aether::rule
