#include "ScriptLog.hpp"

#include <iostream>

namespace sat {

void ScriptLog::clear() {
    m_entries.clear();
}

void ScriptLog::info(std::string_view message) {
    append(LogLevel::Info, message);
    std::clog << "[log] " << message << '\n';
}

void ScriptLog::warn(std::string_view message) {
    append(LogLevel::Warn, message);
    std::cerr << "[warn] " << message << '\n';
}

void ScriptLog::error(std::string_view message) {
    append(LogLevel::Error, message);
    std::cerr << "[error] " << message << '\n';
}

void ScriptLog::append(LogLevel level, std::string_view message) {
    if (m_entries.size() >= kMaxEntries) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back({level, std::string(message)});
}

} // namespace sat