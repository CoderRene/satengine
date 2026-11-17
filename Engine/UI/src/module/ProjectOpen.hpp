#pragma once

#include "Project.hpp"
#include "Scene.hpp"

#include <filesystem>
#include <future>
#include <optional>
#include <string>

namespace sat {

struct ProjectOpenResult {
    Project project;
    std::optional<Scene> scene;
    std::string error;
    bool invalidFolder = false;
};

[[nodiscard]] std::future<ProjectOpenResult> loadEditorProject(std::filesystem::path folder,
    std::filesystem::path currentRoot);

// The caller must keep authored alive and immutable until the job completes.
[[nodiscard]] std::future<ProjectOpenResult> prepareEditorProject(ProjectOpenResult opened,
    Project current, const Scene& authored);

[[nodiscard]] std::string updateEditorProjectLuaTooling(const std::filesystem::path& root);

} // namespace sat
