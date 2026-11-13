#include "SceneRenderData.hpp"

#include <cstring>

namespace sat {

LightInstance makeLightInstance(const Entity& entity, DVec3 renderOrigin) {
    const auto& light = entity.light;
    const auto basis = rotationMatrixEuler(entity.transform.rotation);
    return {toVec3(entity.transform.position - renderOrigin),
        {basis.m[8], basis.m[9], basis.m[10]},
        Vec3{light.color[0], light.color[1], light.color[2]} * light.intensity, light.type, light.range,
        std::cos(light.innerConeDegrees * kDegToRad), std::cos(light.outerConeDegrees * kDegToRad)};
}

Vec3 ambientRadiance(const LightingSettings& settings) {
    return Vec3{settings.ambientColor[0], settings.ambientColor[1], settings.ambientColor[2]} *
        settings.ambientIntensity;
}

void SceneRenderData::update(const Scene& scene, int width, int height) {
    m_camera = GameViewCamera{};
    m_meshes.clear();
    m_lights.clear();
    m_ambient = ambientRadiance(scene.lightingSettings);
    m_environmentPath = scene.lightingSettings.environmentPath;
    m_environmentIntensity = scene.lightingSettings.environmentIntensity;
    m_directionalShadows = scene.lightingSettings.directionalShadows;
    m_shadowResolution = scene.lightingSettings.shadowResolution;
    m_shadowRadius = scene.lightingSettings.shadowRadius;
    m_bloom = scene.lightingSettings.bloom;
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
        if (entity.hasLight) { m_lights.push_back(makeLightInstance(entity, m_camera.renderOrigin)); }
        if (!entity.hasMesh) {
            continue;
        }
        MeshInstance instance{};
        instance.world = matrixFromTransform(entity.transform, m_camera.renderOrigin);
        instance.meshPath = entity.mesh.meshPath;
        instance.materialPath = entity.mesh.materialPath;
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
    draw.lights = m_lights.data();
    draw.lightCount = static_cast<int>(m_lights.size());
    draw.environmentPath = m_environmentPath;
    draw.environmentIntensity = m_environmentIntensity;
    draw.directionalShadows = m_directionalShadows;
    draw.shadowResolution = m_shadowResolution;
    draw.shadowRadius = m_shadowRadius;
    draw.bloom = m_bloom;
    draw.ambient = m_ambient;
    draw.meshes = m_meshes.empty() ? nullptr : m_meshes.data();
    draw.meshCount = static_cast<int>(m_meshes.size());
    return draw;
}

} // namespace sat
