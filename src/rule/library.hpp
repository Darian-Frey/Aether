// Rule library (F-010).
//
// A rule file is the rule's own source with a block of leading comments
// carrying its name, description and palette. `#` opens a comment in the DSL
// and `--` in Lua, so the header is inert in both and the file is handed to
// the front end whole — there is nothing to strip and nothing to keep in
// step.
//
//   # name: Conway's Life
//   # description: The rule that started it.
//   # palette: 1 = #ECF0EE
//   B3/S23
//
// Recognised keys: name, description, dimensions, palette. Anything else in
// the header is prose. Files ending `.lua` go to the Lua front end, `.rule`
// to the DSL.

#pragma once

#include "rule/ir.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aether::rule {

struct PaletteOverride {
    uint16_t               state;
    std::array<uint8_t, 4> rgba;
};

struct LibraryRule {
    std::string id;            // file stem, which is what `--rule @id` names
    std::string name;          // from the header, or the id
    std::string description;
    std::string source;        // the file, header and all
    std::string path;
    bool        isLua = false;
    uint8_t     dimensions = 2;
    std::vector<PaletteOverride> palette;
};

// Parses one file's text. `id` and `isLua` come from its name.
LibraryRule parseRuleFile(std::string id, std::string text, bool isLua);

// Every `.rule` and `.lua` in the first of `directories` that exists,
// sorted by name. Missing directories are skipped, not an error.
std::vector<LibraryRule> loadLibrary(const std::vector<std::string>& directories);

// Writes `source` to `directory/id.(rule|lua)` with a header carrying the
// name and description. Returns an error message on failure.
std::optional<std::string> saveRule(const std::string& directory, const LibraryRule& rule);

}  // namespace aether::rule
