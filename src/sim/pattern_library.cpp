#include "sim/pattern_library.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace aether::sim {

std::vector<LibraryPattern> loadPatternLibrary(const std::vector<std::string>& directories) {
    namespace fs = std::filesystem;
    std::vector<LibraryPattern> out;
    for (const std::string& dir : directories) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const fs::path& p = entry.path();
            if (p.extension() == ".rle" || p.extension() == ".pattern") files.push_back(p);
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& p : files) {
            std::ifstream f(p, std::ios::binary);
            if (!f) continue;
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto parsed = parsePattern(text);
            // A file that will not parse is left out rather than fatal: the
            // library is a convenience, and one bad file should not cost it.
            if (std::holds_alternative<PatternError>(parsed)) continue;
            LibraryPattern entry;
            entry.id = p.stem().string();
            entry.pattern = std::get<Pattern>(std::move(parsed));
            entry.name = entry.pattern.name.value_or(entry.id);
            entry.description = entry.pattern.comment.value_or(std::string{});
            entry.path = p.string();
            out.push_back(std::move(entry));
        }
        if (!out.empty()) break;   // the first directory that has patterns wins
    }
    return out;
}

}  // namespace aether::sim
