#include "rule/library.hpp"

#include "rule/dsl.hpp"
#include "rule/lua.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace aether::rule {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// The comment marker a header line opens with, or nothing.
std::optional<std::string_view> commentBody(std::string_view line) {
    const std::string_view t = trim(line);
    if (t.starts_with("--")) return trim(t.substr(2));
    if (t.starts_with("#"))  return trim(t.substr(1));
    return std::nullopt;
}

std::optional<uint8_t> hexPair(std::string_view s) {
    uint8_t v = 0;
    for (char c : s) {
        v = static_cast<uint8_t>(v << 4);
        if (c >= '0' && c <= '9') v = static_cast<uint8_t>(v | (c - '0'));
        else if (c >= 'a' && c <= 'f') v = static_cast<uint8_t>(v | (c - 'a' + 10));
        else if (c >= 'A' && c <= 'F') v = static_cast<uint8_t>(v | (c - 'A' + 10));
        else return std::nullopt;
    }
    return v;
}

// "1 = #ECF0EE  2 = #C2712Eff" — later entries win, alpha defaults to opaque.
void parsePalette(std::string_view spec, std::vector<PaletteOverride>& out) {
    while (!spec.empty()) {
        const size_t hash = spec.find('#');
        if (hash == std::string_view::npos) return;
        const std::string_view lhs = trim(spec.substr(0, hash));
        const size_t eq = lhs.rfind('=');
        if (eq == std::string_view::npos) return;
        const std::string_view stateText = trim(lhs.substr(0, eq));
        uint32_t state = 0;
        for (char c : stateText) {
            if (c < '0' || c > '9') return;
            state = state * 10 + static_cast<uint32_t>(c - '0');
        }
        std::string_view rest = spec.substr(hash + 1);
        size_t len = 0;
        while (len < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[len]))) ++len;
        if (len != 6 && len != 8) return;
        PaletteOverride entry{static_cast<uint16_t>(state), {0, 0, 0, 255}};
        for (size_t i = 0; i < len / 2; ++i) {
            const auto v = hexPair(rest.substr(i * 2, 2));
            if (!v) return;
            entry.rgba[i] = *v;
        }
        out.push_back(entry);
        spec = rest.substr(len);
    }
}

}  // namespace

LibraryRule parseRuleFile(std::string id, std::string text, bool isLua) {
    LibraryRule rule;
    rule.id = id;
    rule.name = id;
    rule.isLua = isLua;
    rule.source = text;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty()) continue;
        const auto body = commentBody(line);
        if (!body) break;   // the header is the leading run of comments
        const size_t colon = body->find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = trim(body->substr(0, colon));
        const std::string_view value = trim(body->substr(colon + 1));
        if (key == "name") rule.name = value;
        else if (key == "description") rule.description = value;
        else if (key == "dimensions") {
            if (value.size() == 1 && value[0] >= '1' && value[0] <= '3') {
                rule.dimensions = static_cast<uint8_t>(value[0] - '0');
            }
        } else if (key == "palette") {
            parsePalette(value, rule.palette);
        }
    }
    return rule;
}

std::vector<LibraryRule> loadLibrary(const std::vector<std::string>& directories) {
    namespace fs = std::filesystem;
    std::vector<LibraryRule> out;
    for (const std::string& dir : directories) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const fs::path& p = entry.path();
            if (p.extension() == ".rule" || p.extension() == ".lua") files.push_back(p);
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            std::ifstream f(p);
            if (!f) continue;
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            LibraryRule rule = parseRuleFile(p.stem().string(), std::move(text), p.extension() == ".lua");
            rule.path = p.string();
            out.push_back(std::move(rule));
        }
        if (!out.empty()) break;   // the first directory that has rules wins
    }
    return out;
}

std::optional<std::string> saveRule(const std::string& directory, const LibraryRule& rule) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(directory, ec);
    const std::string marker = rule.isLua ? "--" : "#";
    const fs::path path = fs::path(directory) / (rule.id + (rule.isLua ? ".lua" : ".rule"));

    // Keep whatever header the source already carries, and add what it lacks.
    std::string header;
    if (rule.source.find(marker + " name:") == std::string::npos) {
        header += std::format("{} name: {}\n", marker, rule.name);
    }
    if (!rule.description.empty() && rule.source.find(marker + " description:") == std::string::npos) {
        header += std::format("{} description: {}\n", marker, rule.description);
    }
    if (rule.dimensions != 2 && rule.source.find(marker + " dimensions:") == std::string::npos) {
        header += std::format("{} dimensions: {}\n", marker, rule.dimensions);
    }

    std::ofstream f(path, std::ios::trunc);
    if (!f) return std::format("cannot write {}", path.string());
    f << header << rule.source;
    if (!rule.source.ends_with("\n")) f << "\n";
    if (!f) return std::format("write failed for {}", path.string());
    return std::nullopt;
}

std::variant<RuleIR, std::string> compileLibraryRule(const LibraryRule& entry, Boundary boundary) {
    if (entry.isLua) {
        LuaContext lctx;
        lctx.dimensions = entry.dimensions;
        lctx.boundary = boundary;
        auto r = compileLua(entry.source, lctx);
        if (const auto* e = std::get_if<LuaError>(&r)) return e->message;
        return std::get<RuleIR>(std::move(r));
    }
    DslContext ctx;
    ctx.dimensions = entry.dimensions;
    ctx.boundary = boundary;
    auto parsed = parseDsl(entry.source, ctx);
    if (!parsed) {
        return std::format("{}:{}: {}", parsed.error->line, parsed.error->column, parsed.error->message);
    }
    return *parsed.ir;
}

}  // namespace aether::rule
