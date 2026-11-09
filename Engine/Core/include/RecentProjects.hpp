#pragma once

#include <filesystem>
#include <vector>

namespace sat {

class RecentProjects {
public:
    explicit RecentProjects(std::filesystem::path storageFile);

    void load();
    void save() const;

    void add(const std::filesystem::path& projectDir);
    void remove(const std::filesystem::path& projectDir);

    [[nodiscard]] const std::vector<std::filesystem::path>& entries() const { return m_entries; }

private:
    static constexpr std::size_t kMaxEntries = 10;

    std::filesystem::path m_storageFile;
    std::vector<std::filesystem::path> m_entries;
};

} // namespace sat
