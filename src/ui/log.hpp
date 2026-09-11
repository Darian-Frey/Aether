// Ring-buffer log shown in the UI (ARCHITECTURE §Cross-cutting concerns).
// Compilation, backend selection and later mutation events go here. The
// step loop logs nothing.

#pragma once

#include <deque>
#include <string>

namespace aether::ui {

class Log {
public:
    explicit Log(size_t capacity = 200) : capacity_(capacity) {}

    void info(std::string line)  { push("     " + std::move(line)); }
    void error(std::string line) { push("ERR  " + std::move(line)); }

    const std::deque<std::string>& lines() const { return lines_; }
    void clear() { lines_.clear(); }

private:
    void push(std::string line) {
        lines_.push_back(std::move(line));
        while (lines_.size() > capacity_) lines_.pop_front();
    }

    size_t                  capacity_;
    std::deque<std::string> lines_;
};

}  // namespace aether::ui
