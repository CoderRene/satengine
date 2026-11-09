#include "RecentProjects.hpp"

#include <algorithm>
#include <fstream>
#include <string>

namespace sat {
namespace fs = std::filesystem;

RecentProjects::RecentProjects(fs::path storageFile)
    : m_storageFile(std::move(storageFile)) {
    load();
}

void RecentProjects::load() {
    m_entries.clear();

    std::ifstream in(m_storageFile);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        fs::path path(line);
        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            continue;
        }
        m_entries.push_back(std::move(path));
        if (m_entries.size() >= kMaxEntries) {
            break;
        }
    }
}

void RecentProjects::save() const {
    std::ofstream out(m_storageFile, std::ios::trunc);
    if (!out) {
        return;
    }
    for (const fs::path& path : m_entries) {
        out << path.string() << '\n';
    }
}

void RecentProjects::add(const fs::path& projectDir) {
    std::error_code ec;
    const fs::path absolute = fs::weakly_canonical(projectDir, ec);
    const fs::path path = ec ? fs::absolute(projectDir) : absolute;

    m_entries.erase(
        std::remove_if(
            m_entries.begin(),
            m_entries.end(),
            [&](const fs::path& existing) {
                std::error_code compareEc;
                return fs::equivalent(existing, path, compareEc) || existing == path;
            }),
        m_entries.end());

    m_entries.insert(m_entries.begin(), path);
    if (m_entries.size() > kMaxEntries) {
        m_entries.resize(kMaxEntries);
    }
    save();
}

void RecentProjects::remove(const fs::path& projectDir) {
    const auto before = m_entries.size();
    m_entries.erase(
        std::remove_if(
            m_entries.begin(),
            m_entries.end(),
            [&](const fs::path& existing) {
                std::error_code compareEc;
                return fs::equivalent(existing, projectDir, compareEc) || existing == projectDir;
            }),
        m_entries.end());
    if (m_entries.size() != before) {
        save();
    }
}

} // namespace sat
