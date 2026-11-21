#pragma once

#include "Renderer.hpp"
#include "Scene.hpp"

#include <vector>

namespace sat {

// Converts scene state into runtime render data without editor dependencies.
class AnimationSystem;
class SceneRenderData {
public:
    void update(const Scene& scene, int width, int height, const AnimationSystem* animation = nullptr);
    [[nodiscard]] const GameViewCamera& camera() const { return m_camera; }
    [[nodiscard]] bool hasCamera() const { return m_hasCamera; }
    // Borrowed scene inputs and light storage are consumed during submission.
    // Source scene asset-path strings must remain unchanged until submission ends.
    [[nodiscard]] SceneDrawList drawList() const;

private:
    GameViewCamera m_camera;
    const Scene* m_scene = nullptr;
    const AnimationSystem* m_animation = nullptr;
    std::vector<LightInstance> m_lights;
    Vec3 m_ambient;
    std::string_view m_environmentPath;
    float m_environmentIntensity = 1;
    bool m_showEnvironmentSky = false;
    bool m_weightedTransparency = false;
    bool m_directionalShadows = true;
    int m_shadowResolution = 2048;
    float m_shadowDistance = 100.0f;
    int m_shadowCascadeCount = 4;
    float m_shadowSplitLambda = 0.75f;
    BloomSettings m_bloom;
    bool m_hasCamera = false;
};

} // namespace sat
