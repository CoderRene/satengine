#include "ProjectOpen.hpp"

#include "EditorLuaDefs.hpp"
#include "LuaToolingSync.hpp"

#include <utility>

namespace sat {

std::future<ProjectOpenResult> loadEditorProject(std::filesystem::path folder, std::filesystem::path currentRoot) {
    return std::async(std::launch::async, [folder = std::move(folder), currentRoot = std::move(currentRoot)] {
        ProjectOpenResult result;
        if (!result.project.open(folder)) {
            result.invalidFolder = true;
            result.error = "The selected folder does not contain a SatEngine project.";
        } else if (result.project.root() != currentRoot) {
            if (!result.project.loadScene(result.scene.emplace(), result.error)) {
                result.scene.reset();
            }
        }
        return result;
    });
}

std::future<ProjectOpenResult> prepareEditorProject(ProjectOpenResult opened, Project current, const Scene& authored) {
    return std::async(std::launch::async,
        [opened = std::move(opened), current = std::move(current), &authored]() mutable {
            if (current.isOpen() && !current.saveScene(authored, opened.error)) {
                opened.scene.reset();
            } else {
                opened.error = updateEditorProjectLuaTooling(opened.project.root());
            }
            return std::move(opened);
        });
}

std::string updateEditorProjectLuaTooling(const std::filesystem::path& root) {
    const auto defs = editorLuaDefsDirectory();
    if (defs.empty() || !syncProjectLuaTooling(root, defs)) {
        return "Could not update LuaDefs in the project. Check the project folder permissions and editor log.";
    }
    return {};
}

} // namespace sat
