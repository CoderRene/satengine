#include "EditorState.hpp"

#include "AnimationSystem.hpp"
#include "ColliderLines.hpp"
#include "SceneDraw.hpp"
#include <imgui.h>

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sat {
namespace {

void appendAnimationOcclusionSphere(const Entity& entity, DVec3 renderOrigin, std::vector<DebugLine>& lines) {
    if (!entity.hasAnimator || entity.animator.cullingMode != AnimationCullingMode::Automatic) { return; }
    const float radius = animationOcclusionWorldRadius(entity);
    const auto center = toVec3(entity.transform.position - renderOrigin);
    if (!(radius > 0) || !std::isfinite(radius) ||
        !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) { return; }

    constexpr int kSegments = 48;
    constexpr float kTurn = 6.28318530718f;
    constexpr Vec3 kColor{0.2f, 0.85f, 1.0f};
    float a = radius, b = 0;
    for (int i = 1; i <= kSegments; ++i) {
        const float angle = static_cast<float>(i) * kTurn / kSegments;
        const float nextA = i == kSegments ? radius : std::cos(angle) * radius;
        const float nextB = i == kSegments ? 0.0f : std::sin(angle) * radius;
        lines.push_back({center + Vec3{a, b, 0}, center + Vec3{nextA, nextB, 0}, kColor});
        lines.push_back({center + Vec3{a, 0, b}, center + Vec3{nextA, 0, nextB}, kColor});
        lines.push_back({center + Vec3{0, a, b}, center + Vec3{0, nextA, nextB}, kColor});
        a = nextA;
        b = nextB;
    }
}

void mixViewBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
}

template <typename T>
void mixViewPod(std::uint64_t& hash, const T& value) {
    mixViewBytes(hash, &value, sizeof(T));
}

void mixViewText(std::uint64_t& hash, std::string_view text) {
    mixViewBytes(hash, text.data(), text.size());
    mixViewPod(hash, text.size());
}

// Pointer identity is ignored. Equal cameras and draw lists produce the same key.
std::uint64_t viewRenderKey(const GameViewCamera& camera, const SceneDrawList& draw, int width, int height,
    std::uint64_t drawRevision) {
    std::uint64_t hash = 14695981039346656037ull;
    mixViewPod(hash, drawRevision);
    mixViewPod(hash, width);
    mixViewPod(hash, height);
    mixViewPod(hash, camera.renderOrigin);
    mixViewBytes(hash, camera.clearColor, sizeof(camera.clearColor));
    mixViewBytes(hash, camera.view, sizeof(camera.view));
    mixViewBytes(hash, camera.projection, sizeof(camera.projection));
    mixViewText(hash, draw.environmentPath);
    mixViewPod(hash, draw.environmentIntensity);
    mixViewPod(hash, draw.showEnvironmentSky);
    mixViewPod(hash, draw.weightedTransparency);
    mixViewPod(hash, static_cast<unsigned char>(draw.directionalShadows));
    mixViewPod(hash, draw.shadowResolution);
    mixViewPod(hash, draw.shadowDistance);
    mixViewPod(hash, draw.shadowCascadeCount);
    mixViewPod(hash, draw.shadowSplitLambda);
    mixViewPod(hash, static_cast<unsigned char>(draw.bloom.enabled));
    mixViewPod(hash, draw.bloom.threshold);
    mixViewPod(hash, draw.bloom.softKnee);
    mixViewPod(hash, draw.bloom.intensity);
    mixViewPod(hash, draw.bloom.radius);
    mixViewBytes(hash, draw.bloom.tint, sizeof(draw.bloom.tint));
    mixViewPod(hash, draw.bloom.highlightClamp);
    mixViewPod(hash, draw.bloom.quality);
    mixViewPod(hash, draw.lightCount);
    for (int index = 0; index < draw.lightCount && draw.lights != nullptr; ++index) {
        const LightInstance& light = draw.lights[index];
        mixViewPod(hash, light.position);
        mixViewPod(hash, light.direction);
        mixViewPod(hash, light.radiance);
        mixViewPod(hash, static_cast<unsigned char>(light.type));
        mixViewPod(hash, light.range);
        mixViewPod(hash, light.innerCos);
        mixViewPod(hash, light.outerCos);
        mixViewPod(hash, light.entity);
        mixViewPod(hash, light.castShadow);
    }
    mixViewPod(hash, draw.ambient);
    mixViewPod(hash, draw.debugLineCount);
    for (int index = 0; index < draw.debugLineCount && draw.debugLines != nullptr; ++index) {
        mixViewPod(hash, draw.debugLines[index].a);
        mixViewPod(hash, draw.debugLines[index].b);
        mixViewPod(hash, draw.debugLines[index].color);
    }
    mixViewPod(hash, draw.gridOffset);
    if (draw.scene) {
        mixViewPod(hash, draw.scene->editRevision());
        mixViewPod(hash, draw.scene->destructionRevision());
    }
    if (draw.animation) { mixViewPod(hash, draw.animation->poseRevision()); }
    mixViewPod(hash, draw.selected);
    if (draw.selection) {
        for (EntityId id : *draw.selection) { mixViewPod(hash, id); }
    }
    mixViewPod(hash, draw.frustumCount);
    for (int index = 0; index < draw.frustumCount && draw.frustums != nullptr; ++index) {
        const CameraFrustumInstance& frustum = draw.frustums[index];
        mixViewPod(hash, frustum.position);
        mixViewPod(hash, frustum.rotation);
        mixViewPod(hash, frustum.fieldOfView);
        mixViewPod(hash, frustum.nearClip);
        mixViewPod(hash, frustum.farClip);
        mixViewPod(hash, frustum.aspect);
        mixViewPod(hash, static_cast<unsigned char>(frustum.selected));
    }
    mixViewPod(hash, static_cast<unsigned char>(draw.drawGrid));
    mixViewPod(hash, static_cast<unsigned char>(draw.drawEditorOverlay));
    mixViewPod(hash, static_cast<unsigned char>(draw.showTransformGizmo));
    mixViewPod(hash, static_cast<unsigned char>(draw.gizmo.tool));
    mixViewPod(hash, draw.gizmo.origin);
    mixViewPod(hash, draw.gizmo.basis);
    mixViewPod(hash, draw.gizmo.highlightedHandle);
    mixViewPod(hash, static_cast<unsigned char>(draw.gizmo.uniformScaleOnly));
    mixViewPod(hash, static_cast<unsigned char>(draw.gizmo.dragging));
    return hash;
}

} // namespace

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
    state.play.animation().setAssetRevision(state.renderer->contentRevision());
    state.previewAnimation.setAssetRevision(state.renderer->contentRevision());
    if (!state.play.active()) { state.previewAnimation.update(ImGui::GetIO().DeltaTime); }
    state.gameRenderData.update(state.play.viewScene(state.scene), gameViewWidth, gameViewHeight, state.play.active() ? &state.play.animation() : &state.previewAnimation);
    const auto& gameCamera = state.gameRenderData.camera();
    auto gameDraw = state.gameRenderData.drawList();
    gameDraw.captureGpuStats = state.viewport && state.viewport->gameStatsVisible();

    auto& frustumInstances = state.sceneFrustums;
    auto& colliderLines = state.colliderLines;
    auto& lights = state.sceneLights;
    lights.clear();
    frustumInstances.clear(); colliderLines.clear();
    const int selectedEntity = state.hierarchy ? state.hierarchy->selectedIndex() : -1;
    const float sceneAspect =
        (sceneViewHeight > 0)
            ? static_cast<float>(sceneViewWidth) / static_cast<float>(sceneViewHeight)
            : 1.0f;
    const bool showLiveScene = state.play.active() && state.viewport && state.viewport->showingLiveScene();
    const Scene& visibleScene = showLiveScene ? state.play.game() : state.scene;
    const Entity* selected = selectedEntity >= 0 ? state.scene.entity(static_cast<std::size_t>(selectedEntity)) : nullptr;
    const EntityId selectedId = selected ? selected->id() : kInvalidEntityId;
    const auto* selection = state.hierarchy ? &state.hierarchy->selectedIds() : nullptr;
    for (const Entity& entity : visibleScene.entities()) {
        const bool isSelected = selection && selection->contains(entity.id());
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

    const Entity* gizmoEntity = showLiveScene ? visibleScene.entityById(selectedId) : selected;
    if (gizmoEntity) {
        appendAnimationOcclusionSphere(*gizmoEntity, sceneCamera.renderOrigin, colliderLines);
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
    sceneDraw.showEnvironmentSky = visibleScene.lightingSettings.showEnvironmentSky;
    sceneDraw.weightedTransparency = visibleScene.lightingSettings.weightedTransparency;
    sceneDraw.directionalShadows = visibleScene.lightingSettings.directionalShadows;
    sceneDraw.shadowResolution = visibleScene.lightingSettings.shadowResolution;
    sceneDraw.shadowDistance = visibleScene.lightingSettings.shadowDistance;
    sceneDraw.shadowCascadeCount = visibleScene.lightingSettings.shadowCascadeCount;
    sceneDraw.shadowSplitLambda = visibleScene.lightingSettings.shadowSplitLambda;
    sceneDraw.bloom = visibleScene.lightingSettings.bloom;
    sceneDraw.ambient = ambientRadiance(visibleScene.lightingSettings);
    sceneDraw.scene = &visibleScene;
    sceneDraw.animation = showLiveScene ? &state.play.animation() : &state.previewAnimation;
    sceneDraw.selected = selectedId;
    sceneDraw.selection = selection;
    sceneDraw.frustums = frustumInstances.empty() ? nullptr : frustumInstances.data();
    sceneDraw.frustumCount = static_cast<int>(frustumInstances.size());
    sceneDraw.drawGrid = true;
    sceneDraw.drawEditorOverlay = true;
    if (state.viewport && gizmoEntity) {
        sceneDraw.showTransformGizmo = true;
        sceneDraw.gizmo = state.viewport->gizmo(*gizmoEntity, visibleScene);
    }

    // A collapsed panel keeps its last target. Edit mode redraws a visible view only
    // when its camera, contents, size, or GPU assets changed. Play mode always draws.
    const bool sceneVisible = sceneViewWidth >= 1 && sceneViewHeight >= 1;
    const bool gameVisible = state.gameRenderData.hasCamera() && gameViewWidth >= 1 && gameViewHeight >= 1;
    if (state.play.active()) {
        state.sceneViewValid = false;
        state.gameViewValid = false;
        if (sceneVisible) {
            state.renderer->renderSceneView(sceneCamera, sceneDraw);
        }
        if (gameVisible) {
            state.renderer->renderGameView(gameCamera, gameDraw);
            if (state.gameUi) { state.renderer->renderUi(*state.gameUi, UiTarget::GameView); }
            state.viewport->drawGameStats();
        }
        return;
    }

    const auto contentRevision = state.renderer->contentRevision();
    const bool sceneActive = state.viewport != nullptr && state.viewport->sceneViewInteracting();
    const auto sceneRevision = sceneVisible || (gameVisible && sceneDraw.scene == gameDraw.scene)
        ? state.renderer->sceneDrawRevision(visibleScene) : 0;
    const auto gameRevision = gameVisible && gameDraw.scene != sceneDraw.scene
        ? state.renderer->sceneDrawRevision(*gameDraw.scene) : sceneRevision;
    if (sceneVisible) {
        const auto key = viewRenderKey(sceneCamera, sceneDraw, sceneViewWidth, sceneViewHeight, sceneRevision);
        // Navigation has to publish a new image every frame. A cached target stays one step behind.
        if (sceneActive || !state.sceneViewValid || key != state.sceneViewKey || contentRevision != state.sceneContentRevision) {
            state.renderer->renderSceneView(sceneCamera, sceneDraw);
            state.sceneViewKey = key;
            state.sceneContentRevision = contentRevision;
            state.sceneViewValid = true;
        }
    }
    if (gameVisible) {
        const auto key = viewRenderKey(gameCamera, gameDraw, gameViewWidth, gameViewHeight, gameRevision);
        if (gameDraw.captureGpuStats || !state.gameViewValid || key != state.gameViewKey || contentRevision != state.gameContentRevision) {
            state.renderer->renderGameView(gameCamera, gameDraw);
            state.gameViewKey = key;
            state.gameContentRevision = contentRevision;
            state.gameViewValid = true;
        }
        state.viewport->drawGameStats();
    }
}

} // namespace sat
