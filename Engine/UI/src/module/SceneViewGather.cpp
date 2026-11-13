#include "EditorState.hpp"

#include "ColliderLines.hpp"
#include "SceneDraw.hpp"

#include <cmath>

namespace sat {

void EditorSession::renderViews() {
    auto& state = *m_state;
    GameViewCamera sceneCamera{};
    if (state.viewport) {
        state.viewport->fillSceneCamera(sceneCamera);
    }

    const int gameViewWidth = state.viewport ? state.viewport->gameViewWidth() : 0;
    const int gameViewHeight = state.viewport ? state.viewport->gameViewHeight() : 0;
    const int sceneViewWidth = state.viewport ? state.viewport->sceneViewWidth() : 0;
    const int sceneViewHeight = state.viewport ? state.viewport->sceneViewHeight() : 0;

    state.play.applyEdits(state.scene, state.sceneError);
    state.gameRenderData.update(state.play.viewScene(state.scene), gameViewWidth, gameViewHeight);
    const auto& gameCamera = state.gameRenderData.camera();

    auto& meshInstances = state.sceneMeshes;
    auto& frustumInstances = state.sceneFrustums;
    auto& colliderLines = state.colliderLines;
    auto& lights = state.sceneLights;
    lights.clear();
    meshInstances.clear(); frustumInstances.clear(); colliderLines.clear();
    int selectedMesh = -1;
    const int selectedEntity = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
    const float sceneAspect =
        (sceneViewHeight > 0)
            ? static_cast<float>(sceneViewWidth) / static_cast<float>(sceneViewHeight)
            : 1.0f;
    const bool showLiveScene = state.play.active() && state.viewport && state.viewport->showingLiveScene();
    const Scene& visibleScene = showLiveScene ? state.play.game() : state.scene;
    const Entity* selected = selectedEntity >= 0 ? state.scene.entity(static_cast<std::size_t>(selectedEntity)) : nullptr;
    const EntityId selectedId = selected ? selected->id() : kInvalidEntityId;
    for (const Entity& entity : visibleScene.entities()) {
        const bool isSelected = entity.id() == selectedId;
        if (state.viewport && state.viewport->showColliders()) {
            appendColliderLines(entity, sceneCamera.renderOrigin, isSelected, colliderLines);
        }
        if (entity.hasLight && isSelected) {
            const auto light = makeLightInstance(entity, sceneCamera.renderOrigin);
            const Vec3 color{entity.light.color[0], entity.light.color[1], entity.light.color[2]};
            if (light.type != LightType::Point) {
                const float length = light.type == LightType::Directional ? 2.0f : light.range;
                colliderLines.push_back({light.position, light.position + light.direction * length, color});
            }
            if (light.type != LightType::Directional) {
                for (Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
                    colliderLines.push_back({light.position - axis * light.range,
                        light.position + axis * light.range, color});
                }
            }
        }
        if (entity.hasLight) { lights.push_back(makeLightInstance(entity, sceneCamera.renderOrigin)); }
        if (entity.hasMesh) {
            MeshInstance instance{};
            instance.world = matrixFromTransform(entity.transform, sceneCamera.renderOrigin);
            instance.meshPath = entity.mesh.meshPath;
            instance.materialPath = entity.mesh.materialPath;
            switch (entity.mesh.primitive) {
            case MeshComponent::Primitive::Cube:
                instance.primitive = MeshPrimitive::Cube;
                break;
            }
            if (isSelected) {
                selectedMesh = static_cast<int>(meshInstances.size());
            }
            meshInstances.push_back(instance);
        }
        if (entity.hasCamera) {
            CameraFrustumInstance frustum{};
            frustum.position = toVec3(entity.transform.position - sceneCamera.renderOrigin);
            frustum.rotation = entity.transform.rotation;
            frustum.fieldOfView = entity.camera.fieldOfView;
            frustum.nearClip = entity.camera.nearClip;
            frustum.farClip = entity.camera.farClip;
            frustum.aspect = sceneAspect;
            frustum.selected = isSelected;
            frustumInstances.push_back(frustum);
        }
    }

    SceneDrawList sceneDraw{};
    sceneDraw.debugLines = colliderLines.data();
    sceneDraw.debugLineCount = static_cast<int>(colliderLines.size());
    sceneDraw.gridOffset = {-static_cast<float>(std::fmod(sceneCamera.renderOrigin.x, 1.0)),
        -static_cast<float>(sceneCamera.renderOrigin.y), -static_cast<float>(std::fmod(sceneCamera.renderOrigin.z, 1.0))};
    sceneDraw.lights = lights.data();
    sceneDraw.lightCount = static_cast<int>(lights.size());
    sceneDraw.environmentPath = visibleScene.lightingSettings.environmentPath;
    sceneDraw.environmentIntensity = visibleScene.lightingSettings.environmentIntensity;
    sceneDraw.directionalShadows = visibleScene.lightingSettings.directionalShadows;
    sceneDraw.shadowResolution = visibleScene.lightingSettings.shadowResolution;
    sceneDraw.shadowRadius = visibleScene.lightingSettings.shadowRadius;
    sceneDraw.bloom = visibleScene.lightingSettings.bloom;
    sceneDraw.ambient = ambientRadiance(visibleScene.lightingSettings);
    sceneDraw.meshes = meshInstances.empty() ? nullptr : meshInstances.data();
    sceneDraw.meshCount = static_cast<int>(meshInstances.size());
    sceneDraw.selectedMesh = selectedMesh;
    sceneDraw.frustums = frustumInstances.empty() ? nullptr : frustumInstances.data();
    sceneDraw.frustumCount = static_cast<int>(frustumInstances.size());
    sceneDraw.drawGrid = true;
    sceneDraw.drawEditorOverlay = true;
    const Entity* gizmoEntity = showLiveScene ? visibleScene.entityById(selectedId) : selected;
    if (state.viewport && gizmoEntity) {
        sceneDraw.showTransformGizmo = true;
        sceneDraw.gizmo = state.viewport->gizmo(*gizmoEntity);
    }

    state.renderer->renderSceneView(sceneCamera, sceneDraw);
    if (state.gameRenderData.hasCamera()) {
        state.renderer->renderGameView(gameCamera, state.gameRenderData.drawList());
    }
}

} // namespace sat
