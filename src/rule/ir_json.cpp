#include "rule/ir_json.hpp"

#include <nlohmann/json.hpp>

#include <format>

namespace aether::rule {

using nlohmann::json;

// --- base64 --------------------------------------------------------------------

namespace {

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = (uint32_t{bytes[i]} << 16) | (uint32_t{bytes[i + 1]} << 8) | bytes[i + 2];
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
    }
    if (i < bytes.size()) {
        uint32_t v = uint32_t{bytes[i]} << 16;
        if (i + 1 < bytes.size()) v |= uint32_t{bytes[i + 1]} << 8;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kB64[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::variant<std::vector<uint8_t>, std::string> base64Decode(const std::string& text) {
    if (text.size() % 4 != 0) return std::string("base64 length is not a multiple of 4");
    std::vector<uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + static_cast<size_t>(k)];
            if (c == '=') { v[k] = 0; ++pad; }
            else { v[k] = b64Value(c); if (v[k] < 0) return std::format("bad base64 character at {}", i + static_cast<size_t>(k)); }
        }
        const uint32_t w = (static_cast<uint32_t>(v[0]) << 18) | (static_cast<uint32_t>(v[1]) << 12) |
                           (static_cast<uint32_t>(v[2]) << 6) | static_cast<uint32_t>(v[3]);
        out.push_back(static_cast<uint8_t>(w >> 16));
        if (pad < 2) out.push_back(static_cast<uint8_t>(w >> 8));
        if (pad < 1) out.push_back(static_cast<uint8_t>(w));
    }
    return out;
}

// --- IR -> JSON -----------------------------------------------------------------

namespace {

json expressionToJson(const Expression& e) {
    json nodes = json::array();
    for (const ExprNode& n : e.nodes) {
        nodes.push_back({std::string(toString(n.op)), n.a, n.b, n.c, n.ival, n.fval});
    }
    return {{"nodes", nodes}};
}

std::optional<ExprOp> parseExprOp(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(ExprOp::Select); ++i) {
        const auto op = static_cast<ExprOp>(i);
        if (toString(op) == s) return op;
    }
    return std::nullopt;
}

std::variant<Expression, std::string> expressionFromJson(const json& j) {
    if (!j.is_object() || !j.contains("nodes") || !j["nodes"].is_array()) return std::string("expression needs a nodes array");
    Expression e;
    for (const json& n : j["nodes"]) {
        if (!n.is_array() || n.size() != 6) return std::string("expression node must be [op, a, b, c, ival, fval]");
        const auto op = parseExprOp(n[0].get<std::string>());
        if (!op) return std::format("unknown expression op '{}'", n[0].get<std::string>());
        e.nodes.push_back({*op, n[1].get<uint32_t>(), n[2].get<uint32_t>(), n[3].get<uint32_t>(),
                           n[4].get<int64_t>(), n[5].get<float>()});
    }
    return e;
}

}  // namespace

json irToJson(const RuleIR& ir) {
    json j;
    j["ir_version"] = ir.ir_version;
    j["dimensions"] = ir.dimensions;
    j["cell_type"] = std::string(core::toString(ir.cell_type));
    j["states"] = ir.states;
    j["neighbourhood"] = {{"type", std::string(toString(ir.neighbourhood.type))}, {"radius", ir.neighbourhood.radius}};
    j["boundary"] = std::string(toString(ir.boundary));
    j["kind"] = std::string(toString(ir.kind));
    if (const auto* t = std::get_if<Table>(&ir.transition)) {
        j["transition"] = {{"form", "table"}, {"entries", base64Encode(t->entries)}, {"size", t->entries.size()}};
    } else if (const auto* e = std::get_if<Expression>(&ir.transition)) {
        j["transition"] = expressionToJson(*e);
        j["transition"]["form"] = "expression";
    } else if (const auto* k = std::get_if<Kernel>(&ir.transition)) {
        j["transition"] = {{"form", "kernel"},
                           {"shape", k->shape == Kernel::Shape::Radial ? "radial" : "explicit"},
                           {"profile", k->profile},
                           {"growth", expressionToJson(k->growth)}};
    }
    json meta = json::object();
    if (ir.metadata.name) meta["name"] = *ir.metadata.name;
    if (ir.metadata.author) meta["author"] = *ir.metadata.author;
    if (ir.metadata.source_notation) meta["source_notation"] = *ir.metadata.source_notation;
    j["metadata"] = meta;
    return j;
}

std::variant<RuleIR, std::string> irFromJson(const json& j) {
    try {
        if (!j.is_object()) return std::string("rule IR must be an object");
        RuleIR ir;
        ir.ir_version = j.at("ir_version").get<uint16_t>();
        if (ir.ir_version != kIrVersion) return std::format("unsupported ir_version {}", ir.ir_version);
        ir.dimensions = j.at("dimensions").get<uint8_t>();
        const auto ct = core::parseCellType(j.at("cell_type").get<std::string>());
        if (!ct) return std::string("unknown cell_type");
        ir.cell_type = *ct;
        ir.states = j.at("states").get<uint16_t>();
        const auto nt = parseNeighbourhoodType(j.at("neighbourhood").at("type").get<std::string>());
        if (!nt) return std::string("unknown neighbourhood type");
        ir.neighbourhood = {*nt, j.at("neighbourhood").at("radius").get<uint8_t>()};
        const auto bd = parseBoundary(j.at("boundary").get<std::string>());
        if (!bd) return std::string("unknown boundary");
        ir.boundary = *bd;
        const auto kd = parseKind(j.at("kind").get<std::string>());
        if (!kd) return std::string("unknown kind");
        ir.kind = *kd;

        const json& t = j.at("transition");
        const std::string form = t.at("form").get<std::string>();
        if (form == "table") {
            auto bytes = base64Decode(t.at("entries").get<std::string>());
            if (const auto* e = std::get_if<std::string>(&bytes)) return *e;
            Table table;
            table.entries = std::move(std::get<std::vector<uint8_t>>(bytes));
            if (t.contains("size") && t["size"].get<size_t>() != table.entries.size()) {
                return std::string("table size field disagrees with its entries");
            }
            ir.transition = std::move(table);
        } else if (form == "expression") {
            auto e = expressionFromJson(t);
            if (const auto* err = std::get_if<std::string>(&e)) return *err;
            ir.transition = std::move(std::get<Expression>(e));
        } else if (form == "kernel") {
            Kernel k;
            k.shape = t.at("shape").get<std::string>() == "radial" ? Kernel::Shape::Radial : Kernel::Shape::Explicit;
            k.profile = t.at("profile").get<std::vector<float>>();
            auto g = expressionFromJson(t.at("growth"));
            if (const auto* err = std::get_if<std::string>(&g)) return *err;
            k.growth = std::move(std::get<Expression>(g));
            ir.transition = std::move(k);
        } else {
            return std::format("unknown transition form '{}'", form);
        }

        if (j.contains("metadata") && j["metadata"].is_object()) {
            const json& m = j["metadata"];
            if (m.contains("name")) ir.metadata.name = m["name"].get<std::string>();
            if (m.contains("author")) ir.metadata.author = m["author"].get<std::string>();
            if (m.contains("source_notation")) ir.metadata.source_notation = m["source_notation"].get<std::string>();
        }

        if (const auto ds = validate(ir); !ds.empty()) return "invalid rule: " + ds.front().message;
        return ir;
    } catch (const json::exception& e) {
        return std::format("malformed rule IR: {}", e.what());
    }
}

}  // namespace aether::rule
