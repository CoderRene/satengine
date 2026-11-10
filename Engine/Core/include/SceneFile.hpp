#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace sat {
class Scene;

// Versioned UTF-8 text. Load failures preserve the destination scene.
[[nodiscard]] std::string serializeScene(const Scene& scene);
[[nodiscard]] bool deserializeScene(std::string_view data, Scene& scene, std::string& error);
[[nodiscard]] bool loadSceneFile(const std::filesystem::path& path, Scene& scene, std::string& error);
// Validates and replaces through a temporary file; failure preserves the old file.
[[nodiscard]] bool saveSceneFile(const std::filesystem::path& path, const Scene& scene, std::string& error);
} // namespace sat
