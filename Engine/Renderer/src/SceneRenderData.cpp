#include "SceneRenderData.hpp"

#include <cstring>

namespace sat {

void SceneRenderData::update(const Scene& scene, int width, int height) {
    m_camera = GameViewCamera{};
    m_meshes.clear();
    m_hasCamera = false;
    const int index = scene.mainCameraIndex();
    if (index < 0 || width < 1 || height < 1) {
        return;
    }
    const Entity* cameraEntity = scene.entity(static_cast<std::size_t>(index));
    if (cameraEntity == nullptr || !cameraEntity->hasCamera) {
        return;
    }
    m_hasCamera = true;
    const auto& camera = cameraEntity->camera;
    std::memcpy(m_camera.clearColor, camera.clearColor, sizeof(m_camera.clearColor));
    m_camera.renderOrigin = cameraEntity->transform.position;
    const Mat4 view = viewFromEuler(Vec3{}, cameraEntity->transform.rotation);
    const Mat4 projection = perspective(
        camera.fieldOfView, static_cast<float>(width) / static_cast<float>(height),
        camera.nearClip, camera.farClip);
    std::memcpy(m_camera.view, view.m, sizeof(m_camera.view));
    std::memcpy(m_camera.projection, projection.m, sizeof(m_camera.projection));
    for (const Entity& entity : scene.entities()) {
        if (!entity.hasMesh) {
            continue;
        }
        MeshInstance instance{};
        instance.world = matrixFromTransform(entity.transform, m_camera.renderOrigin);
        instance.meshPath = entity.mesh.meshPath;
        instance.texturePath = entity.mesh.texturePath;
        switch (entity.mesh.primitive) {
        case MeshComponent::Primitive::Cube:
            instance.primitive = MeshPrimitive::Cube;
            break;
        }
        m_meshes.push_back(instance);
    }
}

SceneDrawList SceneRenderData::drawList() const {
    SceneDrawList draw{};
    draw.meshes = m_meshes.empty() ? nullptr : m_meshes.data();
    draw.meshCount = static_cast<int>(m_meshes.size());
    return draw;
}

} // namespace sat
