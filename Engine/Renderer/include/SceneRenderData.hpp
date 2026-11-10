#pragma once

#include "Renderer.hpp"
#include "Scene.hpp"

#include <vector>

namespace sat {

// Converts scene state into runtime render data without editor dependencies.
class SceneRenderData {
public:
    void update(const Scene& scene, int width, int height);
    [[nodiscard]] const GameViewCamera& camera() const { return m_camera; }
    [[nodiscard]] bool hasCamera() const { return m_hasCamera; }
    // Borrowed mesh storage remains valid until the next update or destruction.
    [[nodiscard]] SceneDrawList drawList() const;

private:
    GameViewCamera m_camera;
    std::vector<MeshInstance> m_meshes;
    bool m_hasCamera = false;
};

} // namespace sat
