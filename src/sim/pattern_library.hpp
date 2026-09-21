// Bundled pattern library (F-027).
//
// The same arrangement `rule/library` has for rules: a directory of files,
// the first directory that holds any winning, each file its own source with
// no sidecar of metadata to keep in step. A pattern file already carries its
// name, the rule it is meant for and a comment (SPEC §14), so the library
// reads those rather than inventing a header format of its own.
//
// Provenance belongs in the comment, and the distinction matters: a published
// pattern is a *data item* transcribed from somewhere, and one built here is
// not. Each bundled file says which it is.

#pragma once

#include "sim/pattern.hpp"

#include <string>
#include <vector>

namespace aether::sim {

struct LibraryPattern {
    std::string id;            // file stem
    std::string name;          // the pattern's own name, or the id
    std::string description;   // its comment
    std::string path;
    Pattern     pattern;
};

// Every `.rle` and `.pattern` in the first of `directories` that holds any,
// sorted by name. A file that will not parse is skipped rather than fatal:
// one bad pattern should not cost the whole library.
std::vector<LibraryPattern> loadPatternLibrary(const std::vector<std::string>& directories);

}  // namespace aether::sim
