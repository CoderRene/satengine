#pragma once

#include <cstddef>

namespace sat {

class Project;
class Scene;

void drawSceneSettingsWindows(bool& showLighting, bool& showAudio, bool& showPhysics,
    Scene& scene, const Project& project, bool readOnly, bool playMode, std::size_t physicsWorldCount);

} // namespace sat
