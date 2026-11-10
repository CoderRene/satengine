#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

enum class LogLevel { Info, Warn, Error };

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string message;
};

// Window-thread only. Bounds retained messages; always mirrors to clog/cerr.
class ScriptLog {
public:
    void clear();
    void info(std::string_view message);
    void warn(std::string_view message);
    void error(std::string_view message);
    [[nodiscard]] const std::vector<LogEntry>& entries() const { return m_entries; }

private:
    void append(LogLevel level, std::string_view message);
    std::vector<LogEntry> m_entries;
    static constexpr std::size_t kMaxEntries = 1000;
};

} // namespace sat