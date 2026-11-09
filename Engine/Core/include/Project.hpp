#pragma once

#include <filesystem>
#include <string>

namespace sat {

class Project {
public:
    [[nodiscard]] bool isOpen() const { return !m_root.empty(); }
    [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

    // Creates parentDir / name, writes SatEngineProject.sat + Assets/, then opens it.
    [[nodiscard]] bool create(const std::filesystem::path& parentDir, const std::string& name);

    // Opens an existing folder that contains SatEngineProject.sat.
    [[nodiscard]] bool open(const std::filesystem::path& projectDir);

    void close() { m_root.clear(); }

private:
    std::filesystem::path m_root;
};

} // namespace sat
