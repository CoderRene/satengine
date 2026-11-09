#include "Project.hpp"

#include <fstream>
#include <iostream>

namespace sat {
namespace fs = std::filesystem;

static fs::path markerPath(const fs::path& dir) {
    return dir / "SatEngineProject.sat";
}

bool Project::create(const fs::path& parentDir, const std::string& name) {
    if (name.empty() || !fs::is_directory(parentDir)) {
        std::cerr << "Invalid parent folder or empty project name\n";
        return false;
    }

    if (name.find_first_of("\\/:") != std::string::npos) {
        std::cerr << "Project name must not contain path separators\n";
        return false;
    }

    const fs::path projectDir = parentDir / name;
    if (fs::exists(projectDir)) {
        std::cerr << "Project folder already exists: " << projectDir << '\n';
        return false;
    }

    std::error_code ec;
    fs::create_directories(projectDir, ec);
    if (ec || !fs::is_directory(projectDir)) {
        std::cerr << "Failed to create project folder: " << projectDir << '\n';
        return false;
    }

    std::ofstream marker(markerPath(projectDir));
    if (!marker) {
        std::cerr << "Failed to write SatEngineProject.sat in: " << projectDir << '\n';
        return false;
    }
    marker << "name=" << name << '\n';

    fs::create_directories(projectDir / "Assets", ec);
    return open(projectDir);
}

bool Project::open(const fs::path& projectDir) {
    if (!fs::is_directory(projectDir) || !fs::exists(markerPath(projectDir))) {
        std::cerr << "Not a SatEngine project: " << projectDir << '\n';
        return false;
    }
    m_root = fs::absolute(projectDir);
    return true;
}

} // namespace sat
