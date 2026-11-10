#include "Project.hpp"

#include "Scene.hpp"
#include "SceneFile.hpp"

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

    if (name == "." || name == ".." || name.find_first_of("\\/:") != std::string::npos) {
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

    marker.close();
    if (!marker) {
        std::cerr << "Failed to finish project marker: " << projectDir << '\n';
        return false;
    }
    fs::create_directories(projectDir / "Assets", ec);
    if (!ec) {
        fs::create_directories(projectDir / "Scripts", ec);
    }
    if (ec) {
        std::cerr << "Failed to create project folders: " << ec.message() << '\n';
        return false;
    }
    std::ofstream script(projectDir / "Scripts" / "main.lua");
    script << R"lua(-- Attach this file to an entity in Inspector > Add Component > Lua Script.
function onUpdate(deltaSeconds)
    local x, y, z = sat.getRotation(self.entity)
    sat.setRotation(self.entity, x, (y + 45 * deltaSeconds) % 360, z)
end
)lua";
    script.close();
    if (!script) {
        std::cerr << "Failed to write Scripts/main.lua in: " << projectDir << '\n';
        return false;
    }
    Scene scene;
    std::string error;
    if (!saveSceneFile(projectDir / "Scene.sat", scene, error)) {
        std::cerr << error << '\n';
        return false;
    }
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

bool Project::loadScene(Scene& scene, std::string& error) const {
    if (!isOpen()) {
        error = "No project is open.";
        return false;
    }
    std::error_code ec;
    const bool exists = fs::exists(m_root / "Scene.sat", ec);
    if (ec) {
        error = "Cannot inspect Scene.sat: " + ec.message();
        return false;
    }
    if (!exists) {
        scene = Scene{}; // Older projects start with a camera; no script is auto-attached.
        error.clear();
        return true;
    }
    return loadSceneFile(m_root / "Scene.sat", scene, error);
}

bool Project::saveScene(const Scene& scene, std::string& error) const {
    if (!isOpen()) {
        error = "No project is open.";
        return false;
    }
    return saveSceneFile(m_root / "Scene.sat", scene, error);
}

} // namespace sat
