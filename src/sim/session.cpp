#include "sim/session.hpp"

#include "rule/ir_json.hpp"
#include "sim/pattern.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace aether::sim {

using nlohmann::json;

// --- Cell codec ------------------------------------------------------------------

namespace {

std::vector<uint8_t> runLengthEncode(const std::vector<uint8_t>& cells) {
    std::vector<uint8_t> rle;
    rle.reserve(cells.size() / 4);
    size_t i = 0;
    while (i < cells.size()) {
        const uint8_t v = cells[i];
        size_t run = 1;
        while (i + run < cells.size() && cells[i + run] == v && run < 255) ++run;
        rle.push_back(static_cast<uint8_t>(run));
        rle.push_back(v);
        i += run;
    }
    return rle;
}

}  // namespace

EncodedCells encodeCells(const std::vector<uint8_t>& cells) {
    const std::vector<uint8_t> rle = runLengthEncode(cells);
    // Two bytes a run, so a buffer with no runs comes back twice its size.
    // A quiescent u8 grid compresses enormously and a float grid not at all
    // (IMP-006), and there is no need to guess which this is: measure.
    if (rle.size() <= cells.size()) return {"rle", rule::base64Encode(rle)};
    return {"bytes", rule::base64Encode(cells)};
}

std::variant<std::vector<uint8_t>, SessionError> decodeCells(const std::string& encoding,
                                                             const std::string& text,
                                                             size_t expectedBytes) {
    auto bytes = rule::base64Decode(text);
    if (const auto* e = std::get_if<std::string>(&bytes)) return SessionError{*e};
    std::vector<uint8_t> out;
    if (encoding == "bytes") {
        out = std::move(std::get<std::vector<uint8_t>>(bytes));
    } else if (encoding == "rle") {
        const auto& rle = std::get<std::vector<uint8_t>>(bytes);
        if (rle.size() % 2 != 0) return SessionError{"cell data has an odd byte count"};
        out.reserve(expectedBytes);
        for (size_t i = 0; i < rle.size(); i += 2) {
            out.insert(out.end(), rle[i], rle[i + 1]);
        }
    } else {
        return SessionError{std::format("unsupported cell encoding '{}'", encoding)};
    }
    if (out.size() != expectedBytes) {
        return SessionError{std::format("cell data decodes to {} bytes; the grid needs {}", out.size(), expectedBytes)};
    }
    return out;
}

// --- JSON --------------------------------------------------------------------------

namespace {

std::string hex(uint64_t v) { return std::format("{:#018x}", v); }

std::variant<uint64_t, SessionError> parseHex(const std::string& s) {
    try { return std::stoull(s, nullptr, 16); }
    catch (...) { return SessionError{"bad hex value '" + s + "'"}; }
}

json eventToJson(const Event& ev) {
    json j;
    j["generation"] = ev.generation;
    std::visit([&](const auto& b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, EvSetRule>)       { j["type"] = "set_rule"; j["ir"] = rule::irToJson(b.ir); }
        else if constexpr (std::is_same_v<T, EvRewind>)   { j["type"] = "rewind"; j["entry"] = b.entry; }
        else if constexpr (std::is_same_v<T, EvPaint>)    { j["type"] = "paint"; j["x0"] = b.x0; j["x1"] = b.x1; j["y"] = b.y; j["z"] = b.z; j["state"] = b.state; }
        else if constexpr (std::is_same_v<T, EvFill>)     { j["type"] = "fill"; j["density"] = b.density; }
        else if constexpr (std::is_same_v<T, EvClear>)    { j["type"] = "clear"; }
        else if constexpr (std::is_same_v<T, EvPlace>)    {
            // The native form of §14, nested rather than stringified, so a
            // session stays one readable document.
            j["type"] = "place"; j["x"] = b.x; j["y"] = b.y; j["z"] = b.z;
            j["pattern"] = json::parse(writeNative(b.pattern));
        }
        else if constexpr (std::is_same_v<T, EvCellMutation>) { j["type"] = "cell_mutation"; j["p"] = b.p; j["block"] = b.blockShift; }
        else if constexpr (std::is_same_v<T, EvRuleMutation>) {
            j["type"] = "rule_mutation"; j["enabled"] = b.params.enabled;
            j["interval"] = b.params.interval; j["magnitude"] = b.params.magnitude;
        }
    }, ev.body);
    return j;
}

std::variant<Event, SessionError> eventFromJson(const json& j) {
    Event ev;
    ev.generation = j.at("generation").get<uint64_t>();
    const std::string type = j.at("type").get<std::string>();
    if (type == "set_rule") {
        auto ir = rule::irFromJson(j.at("ir"));
        if (const auto* e = std::get_if<std::string>(&ir)) return SessionError{"journal set_rule: " + *e};
        ev.body = EvSetRule{std::get<rule::RuleIR>(std::move(ir))};
    } else if (type == "rewind") {
        ev.body = EvRewind{j.at("entry").get<size_t>()};
    } else if (type == "paint") {
        ev.body = EvPaint{j.at("x0").get<uint32_t>(), j.at("x1").get<uint32_t>(), j.at("y").get<uint32_t>(),
                          j.at("z").get<uint32_t>(), j.at("state").get<uint8_t>()};
    } else if (type == "fill") {
        ev.body = EvFill{j.at("density").get<std::vector<double>>()};
    } else if (type == "clear") {
        ev.body = EvClear{};
    } else if (type == "place") {
        auto pat = parseNative(j.at("pattern").dump());
        if (const auto* e = std::get_if<PatternError>(&pat)) return SessionError{"journal place: " + e->message};
        ev.body = EvPlace{std::get<Pattern>(std::move(pat)), j.at("x").get<uint32_t>(),
                          j.at("y").get<uint32_t>(), j.at("z").get<uint32_t>()};
    } else if (type == "cell_mutation") {
        ev.body = EvCellMutation{j.at("p").get<double>(), j.value("block", uint8_t{0})};
    } else if (type == "rule_mutation") {
        ev.body = EvRuleMutation{{j.at("enabled").get<bool>(), j.at("interval").get<uint32_t>(), j.at("magnitude").get<uint32_t>()}};
    } else {
        return SessionError{"unknown journal event type '" + type + "'"};
    }
    return ev;
}

const char* originName(LineageOrigin o) {
    switch (o) {
        case LineageOrigin::Initial:  return "initial";
        case LineageOrigin::User:     return "user";
        case LineageOrigin::Mutation: return "mutation";
        case LineageOrigin::Rewind:   return "rewind";
    }
    return "user";
}

std::optional<LineageOrigin> parseOrigin(const std::string& s) {
    if (s == "initial")  return LineageOrigin::Initial;
    if (s == "user")     return LineageOrigin::User;
    if (s == "mutation") return LineageOrigin::Mutation;
    if (s == "rewind")   return LineageOrigin::Rewind;
    return std::nullopt;
}

// SPEC §9.3: full IR for the initial and pinned entries, a delta otherwise.
// A table delta is the list of (index, value) that differ from the previous
// entry; anything else is stored in full.
json lineageToJson(const std::vector<LineageEntry>& entries) {
    json arr = json::array();
    for (size_t i = 0; i < entries.size(); ++i) {
        const LineageEntry& e = entries[i];
        json j;
        j["generation"] = e.generation;
        j["ir_hash"] = hex(e.ir_hash);
        j["pinned"] = e.pinned;
        if (e.name) j["name"] = *e.name;
        j["origin"] = originName(e.origin);
        if (e.rewound_from) j["rewound_from"] = *e.rewound_from;
        j["journal_index"] = e.journal_index;

        const auto* cur = std::get_if<rule::Table>(&e.ir.transition);
        const auto* prev = i > 0 ? std::get_if<rule::Table>(&entries[i - 1].ir.transition) : nullptr;
        const bool sameShape = cur && prev && cur->entries.size() == prev->entries.size() &&
                               e.ir.states == entries[i - 1].ir.states &&
                               e.ir.neighbourhood == entries[i - 1].ir.neighbourhood &&
                               e.ir.boundary == entries[i - 1].ir.boundary && e.ir.kind == entries[i - 1].ir.kind;
        if (i > 0 && !e.pinned && sameShape) {
            json delta = json::array();
            for (size_t k = 0; k < cur->entries.size(); ++k) {
                if (cur->entries[k] != prev->entries[k]) delta.push_back({k, cur->entries[k]});
            }
            j["delta"] = delta;
            json meta = json::object();
            if (e.ir.metadata.name) meta["name"] = *e.ir.metadata.name;
            if (e.ir.metadata.author) meta["author"] = *e.ir.metadata.author;
            if (e.ir.metadata.source_notation) meta["source_notation"] = *e.ir.metadata.source_notation;
            if (e.ir.metadata.decay_from) meta["decay_from"] = *e.ir.metadata.decay_from;
            j["metadata"] = meta;
        } else {
            j["ir"] = rule::irToJson(e.ir);
        }
        arr.push_back(j);
    }
    return arr;
}

std::variant<std::vector<LineageEntry>, SessionError> lineageFromJson(const json& arr) {
    std::vector<LineageEntry> out;
    for (const json& j : arr) {
        LineageEntry e;
        e.generation = j.at("generation").get<uint64_t>();
        auto h = parseHex(j.at("ir_hash").get<std::string>());
        if (const auto* err = std::get_if<SessionError>(&h)) return *err;
        e.ir_hash = std::get<uint64_t>(h);
        e.pinned = j.value("pinned", false);
        if (j.contains("name")) e.name = j["name"].get<std::string>();
        const auto origin = parseOrigin(j.value("origin", "user"));
        if (!origin) return SessionError{"unknown lineage origin"};
        e.origin = *origin;
        if (j.contains("rewound_from")) e.rewound_from = j["rewound_from"].get<size_t>();
        e.journal_index = j.value("journal_index", size_t{0});

        if (j.contains("ir")) {
            auto ir = rule::irFromJson(j["ir"]);
            if (const auto* err = std::get_if<std::string>(&ir)) return SessionError{"lineage: " + *err};
            e.ir = std::get<rule::RuleIR>(std::move(ir));
        } else if (j.contains("delta")) {
            if (out.empty()) return SessionError{"lineage delta with no previous entry"};
            e.ir = out.back().ir;
            auto* t = std::get_if<rule::Table>(&e.ir.transition);
            if (!t) return SessionError{"lineage delta on a non-table rule"};
            for (const json& d : j["delta"]) {
                const size_t k = d.at(0).get<size_t>();
                if (k >= t->entries.size()) return SessionError{"lineage delta index out of range"};
                t->entries[k] = d.at(1).get<uint8_t>();
            }
            e.ir.metadata = {};
            if (j.contains("metadata")) {
                const json& m = j["metadata"];
                if (m.contains("name")) e.ir.metadata.name = m["name"].get<std::string>();
                if (m.contains("author")) e.ir.metadata.author = m["author"].get<std::string>();
                if (m.contains("source_notation")) e.ir.metadata.source_notation = m["source_notation"].get<std::string>();
                if (m.contains("decay_from")) e.ir.metadata.decay_from = m["decay_from"].get<uint16_t>();
            }
            if (const auto ds = rule::validate(e.ir); !ds.empty()) return SessionError{"lineage delta produced an invalid rule: " + ds.front().message};
        } else {
            return SessionError{"lineage entry has neither ir nor delta"};
        }
        if (rule::irHash(e.ir) != e.ir_hash) {
            return SessionError{std::format("lineage entry at generation {} does not match its hash", e.generation)};
        }
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace

std::string sessionToJson(const Session& s) {
    json j;
    j["format_version"] = kSessionFormatVersion;
    j["grid"] = {{"dimensions", s.spec.dimensions}, {"w", s.spec.width}, {"h", s.spec.height}, {"d", s.spec.depth},
                 {"cell_type", std::string(core::toString(s.spec.cell_type))},
                 {"boundary", std::string(rule::toString(s.boundary))}};
    const EncodedCells initial = encodeCells(s.initial);
    j["initial"] = {{"encoding", initial.encoding}, {"data", initial.data}};
    j["rule"] = {{"ir", rule::irToJson(s.rule)}, {"ir_hash", hex(rule::irHash(s.rule))}};
    if (s.rule.metadata.source_notation) j["rule"]["source_notation"] = *s.rule.metadata.source_notation;
    j["rng"] = {{"seed_a", s.seedA}, {"seed_b", s.seedB}};
    if (s.streamA) j["rng"]["stream_a_state"] = {hex(s.streamA->state), hex(s.streamA->inc)};
    j["mutation"] = {{"rule", {{"interval", s.ruleMutation.interval}, {"magnitude", s.ruleMutation.magnitude}, {"enabled", s.ruleMutation.enabled}}},
                     {"cell", {{"p", s.cellMutationP}, {"block", s.cellMutationBlock}, {"enabled", s.cellMutationP > 0.0}}}};
    json journal = json::array();
    for (const Event& ev : s.journal) journal.push_back(eventToJson(ev));
    j["journal"] = journal;
    j["lineage"] = lineageToJson(s.lineage);
    j["generation"] = s.generation;
    j["counters"] = {{"rule_mutations", s.ruleMutationsApplied}, {"rule_mutations_skipped", s.ruleMutationsSkipped}};
    if (!s.current.empty()) {
        const EncodedCells state = encodeCells(s.current);
        j["state"] = {{"encoding", state.encoding}, {"data", state.data}};
    }
    return j.dump(1);
}

std::variant<Session, SessionError> sessionFromJson(const std::string& text) {
    try {
        const json j = json::parse(text);
        const int version = j.at("format_version").get<int>();
        if (version != kSessionFormatVersion) {
            return SessionError{std::format("session format_version {} is not supported (this build reads {})", version, kSessionFormatVersion)};
        }
        Session s;
        const json& g = j.at("grid");
        s.spec.dimensions = g.at("dimensions").get<uint8_t>();
        s.spec.width = g.at("w").get<uint32_t>();
        s.spec.height = g.at("h").get<uint32_t>();
        s.spec.depth = g.at("d").get<uint32_t>();
        const auto ct = core::parseCellType(g.at("cell_type").get<std::string>());
        if (!ct) return SessionError{"unknown cell_type"};
        s.spec.cell_type = *ct;
        if (const auto problems = s.spec.problems(); !problems.empty()) return SessionError{"grid: " + problems.front()};
        const auto bd = rule::parseBoundary(g.value("boundary", "wrap"));
        if (!bd) return SessionError{"unknown boundary"};
        s.boundary = *bd;

        const json& init = j.at("initial");
        const auto initialEncoding = init.at("encoding").get<std::string>();
        if (initialEncoding != "raw") {   // raw: filled in by loadSession from the sidecar
            auto cells = decodeCells(initialEncoding, init.at("data").get<std::string>(), s.spec.bytesPerBuffer());
            if (const auto* e = std::get_if<SessionError>(&cells)) return *e;
            s.initial = std::move(std::get<std::vector<uint8_t>>(cells));
        }

        auto ir = rule::irFromJson(j.at("rule").at("ir"));
        if (const auto* e = std::get_if<std::string>(&ir)) return SessionError{"rule: " + *e};
        s.rule = std::get<rule::RuleIR>(std::move(ir));
        if (j["rule"].contains("ir_hash")) {
            auto h = parseHex(j["rule"]["ir_hash"].get<std::string>());
            if (const auto* e = std::get_if<SessionError>(&h)) return *e;
            if (std::get<uint64_t>(h) != rule::irHash(s.rule)) return SessionError{"rule does not match its ir_hash"};
        }

        s.seedA = j.at("rng").at("seed_a").get<uint64_t>();
        s.seedB = j.at("rng").at("seed_b").get<uint64_t>();
        if (j["rng"].contains("stream_a_state")) {
            auto a = parseHex(j["rng"]["stream_a_state"].at(0).get<std::string>());
            auto b = parseHex(j["rng"]["stream_a_state"].at(1).get<std::string>());
            if (const auto* e = std::get_if<SessionError>(&a)) return *e;
            if (const auto* e = std::get_if<SessionError>(&b)) return *e;
            s.streamA = Pcg32::State{std::get<uint64_t>(a), std::get<uint64_t>(b)};
        }

        const json& m = j.at("mutation");
        s.ruleMutation = {m.at("rule").at("enabled").get<bool>(), m.at("rule").at("interval").get<uint32_t>(),
                          m.at("rule").at("magnitude").get<uint32_t>()};
        s.cellMutationP = m.at("cell").at("enabled").get<bool>() ? m.at("cell").at("p").get<double>() : 0.0;
        s.cellMutationBlock = m.at("cell").value("block", uint8_t{0});

        for (const json& ej : j.value("journal", json::array())) {
            auto ev = eventFromJson(ej);
            if (const auto* e = std::get_if<SessionError>(&ev)) return *e;
            s.journal.push_back(std::move(std::get<Event>(ev)));
        }
        auto lin = lineageFromJson(j.value("lineage", json::array()));
        if (const auto* e = std::get_if<SessionError>(&lin)) return *e;
        s.lineage = std::move(std::get<std::vector<LineageEntry>>(lin));

        s.generation = j.value("generation", uint64_t{0});
        if (j.contains("counters")) {
            s.ruleMutationsApplied = j["counters"].value("rule_mutations", uint64_t{0});
            s.ruleMutationsSkipped = j["counters"].value("rule_mutations_skipped", uint64_t{0});
        }
        if (j.contains("state") && j["state"].at("encoding") != "raw") {
            auto cells = decodeCells(j["state"].at("encoding").get<std::string>(),
                                     j["state"].at("data").get<std::string>(), s.spec.bytesPerBuffer());
            if (const auto* e = std::get_if<SessionError>(&cells)) return *e;
            s.current = std::move(std::get<std::vector<uint8_t>>(cells));
        }
        return s;
    } catch (const json::exception& e) {
        return SessionError{std::format("malformed session: {}", e.what())};
    }
}

// --- Files ---------------------------------------------------------------------------

namespace {

std::optional<SessionError> writeFile(const std::string& path, const void* data, size_t bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return SessionError{"cannot open " + path + " for writing"};
    f.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!f) return SessionError{"write failed for " + path};
    return std::nullopt;
}

std::variant<std::vector<uint8_t>, SessionError> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return SessionError{"cannot open " + path};
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return out;
}

}  // namespace

std::optional<SessionError> saveSession(const std::string& path, const Session& s) {
    if (s.spec.bytesPerBuffer() > kInlineByteLimit) {
        // Sidecar for big grids: initial then current, raw bytes.
        Session copy = s;
        std::vector<uint8_t> raw = s.initial;
        raw.insert(raw.end(), s.current.begin(), s.current.end());
        if (auto e = writeFile(path + ".grid", raw.data(), raw.size())) return e;
        copy.initial.clear();
        copy.current.clear();
        std::string text = sessionToJson(copy);
        // Patch the encodings to point at the sidecar.
        json j = json::parse(text);
        j["initial"] = {{"encoding", "raw"}, {"data", std::filesystem::path(path + ".grid").filename().string()}};
        if (!s.current.empty()) j["state"] = {{"encoding", "raw"}, {"data", std::filesystem::path(path + ".grid").filename().string()}, {"offset", s.initial.size()}};
        else j.erase("state");
        text = j.dump(1);
        return writeFile(path, text.data(), text.size());
    }
    const std::string text = sessionToJson(s);
    return writeFile(path, text.data(), text.size());
}

std::variant<Session, SessionError> loadSession(const std::string& path) {
    auto bytes = readFile(path);
    if (const auto* e = std::get_if<SessionError>(&bytes)) return *e;
    const auto& b = std::get<std::vector<uint8_t>>(bytes);
    auto parsed = sessionFromJson(std::string(b.begin(), b.end()));
    if (const auto* e = std::get_if<SessionError>(&parsed)) return *e;
    Session s = std::move(std::get<Session>(parsed));

    if (s.initial.empty()) {
        // Raw sidecar.
        try {
            const json j = json::parse(std::string(b.begin(), b.end()));
            const std::string sidecar = (std::filesystem::path(path).parent_path() / j["initial"]["data"].get<std::string>()).string();
            auto raw = readFile(sidecar);
            if (const auto* e = std::get_if<SessionError>(&raw)) return *e;
            const auto& r = std::get<std::vector<uint8_t>>(raw);
            const size_t n = s.spec.bytesPerBuffer();
            if (r.size() < n) return SessionError{"sidecar is shorter than the grid"};
            s.initial.assign(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n));
            if (j.contains("state") && r.size() >= 2 * n) {
                s.current.assign(r.begin() + static_cast<std::ptrdiff_t>(n), r.begin() + static_cast<std::ptrdiff_t>(2 * n));
            }
        } catch (const json::exception& e) {
            return SessionError{std::format("malformed session: {}", e.what())};
        }
    }
    return s;
}

}  // namespace aether::sim
