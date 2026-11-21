#include "ViewportPanel.hpp"

#include "EnginePaths.hpp"
#include "GameUi.hpp"
#include "Math.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include "TransformGizmo.hpp"
#include "module/SceneCameraSpeed.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace sat {
namespace {

constexpr float kEntityIconSize = 32.0f;

std::uintptr_t loadIconTexture(Renderer& renderer, const char* assetRelativePath) {
    const std::string absPath = std::string(SAT_ENGINE_ASSETS_DIR) + "/" + assetRelativePath;
    const char* paths[] = {absPath.c_str(), assetRelativePath};

    for (const char* path : paths) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
        if (pixels == nullptr) {
            continue;
        }

        const std::uintptr_t texture = renderer.createTexture(pixels, width, height);
        stbi_image_free(pixels);
        if (texture != 0) {
            return texture;
        }
    }

    std::cerr << "Failed to load icon: " << assetRelativePath << '\n';
    return 0;
}

void destroyIconTexture(Renderer& renderer, std::uintptr_t& texture) {
    if (texture != 0) {
        renderer.destroyTexture(texture);
        texture = 0;
    }
}

EntityId drawEntityIcons(
    ImDrawList* draw,
    const ImVec2& imageMin,
    const Mat4& view,
    const Mat4& projection,
    float viewW,
    float viewH,
    const Scene& scene,
    int selectedIndex,
    std::uintptr_t cameraIcon, const std::array<std::uintptr_t, 3>& lightIcons, DVec3 renderOrigin,
    const ImVec2* clickPosition) {
    EntityId clickedLight = kInvalidEntityId;

    const auto& entities = scene.entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& entity = entities[i];
        const auto camera = entity.hasCamera ? cameraIcon : 0;
        std::uintptr_t light = 0;
        if (entity.hasLight) {
            switch (entity.light.type) {
            case LightType::Directional: light = lightIcons[0]; break;
            case LightType::Point: light = lightIcons[1]; break;
            case LightType::Spot: light = lightIcons[2]; break;
            }
        }
        if (camera == 0 && light == 0) {
            continue;
        }

        float sx = 0.0f;
        float sy = 0.0f;
        if (!projectWorldToScreen(
                toVec3(entities[i].transform.position - renderOrigin),
                view,
                projection,
                viewW,
                viewH,
                sx,
                sy)) {
            continue;
        }

        const float width = camera != 0 && light != 0 ? 2 * kEntityIconSize : kEntityIconSize;
        const ImVec2 min(imageMin.x + sx - width * 0.5f, imageMin.y + sy - kEntityIconSize * 0.5f);
        const ImVec2 max(min.x + width, min.y + kEntityIconSize);
        if (camera != 0) {
            draw->AddImage(ImTextureRef((ImTextureID)camera), min, ImVec2(min.x + kEntityIconSize, max.y));
        }
        if (light != 0) {
            const ImVec2 lightMin(max.x - kEntityIconSize, min.y);
            draw->AddImage(ImTextureRef((ImTextureID)light), lightMin, max);
            // Later icons draw on top and take precedence when they overlap.
            if (clickPosition && clickPosition->x >= lightMin.x && clickPosition->x < max.x &&
                clickPosition->y >= lightMin.y && clickPosition->y < max.y) {
                clickedLight = entity.id();
            }
        }

        if (static_cast<int>(i) == selectedIndex) {
            draw->AddRect(
                ImVec2(min.x - 1.0f, min.y - 1.0f),
                ImVec2(max.x + 1.0f, max.y + 1.0f),
                IM_COL32(255, 170, 40, 255),
                2.0f,
                0,
                2.0f);
        }
    }
    return clickedLight;
}

Mat4 cameraPickWorld(const Vec3& position) {
    return translationMatrix(position) *
        scalingMatrix(Vec3{kCameraPickSize, kCameraPickSize, kCameraPickSize});
}

bool intersectRayBounds(const Ray& worldRay, const Mat4& world, float& outT,
    const Vec3& minimum = {-0.5f, -0.5f, -0.5f}, const Vec3& maximum = {0.5f, 0.5f, 0.5f}) {
    const Mat4 invWorld = inverse(world);
    const Vec3 origin = transformPoint(invWorld, worldRay.origin);
    // Keep direction unnormalized so slab t matches the world-ray parameter.
    const Vec3 direction = transformDirection(invWorld, worldRay.direction);

    float tMin = 0.0f;
    float tMax = 1.0e30f;
    const float bounds[3][2] = {{minimum.x, maximum.x}, {minimum.y, maximum.y}, {minimum.z, maximum.z}};
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 0.000001f) {
            if (o[i] < bounds[i][0] || o[i] > bounds[i][1]) {
                return false;
            }
            continue;
        }
        float t1 = (bounds[i][0] - o[i]) / d[i];
        float t2 = (bounds[i][1] - o[i]) / d[i];
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tMin = (std::max)(tMin, t1);
        tMax = (std::min)(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }

    if (tMin < 0.0f) {
        return false;
    }
    outT = tMin;
    return true;
}

void drawSceneOrientationGizmo(
    ImDrawList* draw,
    const ImVec2& imageMin,
    const ImVec2& imageMax,
    float yawDegrees,
    float pitchDegrees) {
    constexpr float kRadius = 36.0f;
    constexpr float kAxisLen = 28.0f;
    constexpr float kPad = 12.0f;
    const ImVec2 center(
        imageMax.x - kPad - kRadius,
        imageMin.y + kPad + kRadius);

    const float pitch = pitchDegrees * kDegToRad;
    const float yaw = yawDegrees * kDegToRad;
    const float cosPitch = std::cos(pitch);
    const Vec3 forward = normalize(Vec3{
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    });
    const Vec3 right = normalize(cross(Vec3{0.0f, 1.0f, 0.0f}, forward));
    const Vec3 up = cross(forward, right);

    struct AxisTip {
        float depth = 0.0f;
        ImVec2 tip{};
        ImU32 color = 0;
        const char* label = "";
    };

    const auto project = [&](const Vec3& axis) {
        return AxisTip{
            dot(axis, forward),
            ImVec2(
                center.x + dot(axis, right) * kAxisLen,
                center.y - dot(axis, up) * kAxisLen),
            0,
            "",
        };
    };

    AxisTip axes[3] = {
        project(Vec3{1.0f, 0.0f, 0.0f}),
        project(Vec3{0.0f, 1.0f, 0.0f}),
        project(Vec3{0.0f, 0.0f, 1.0f}),
    };
    axes[0].color = IM_COL32(242, 64, 64, 255);
    axes[0].label = "X";
    axes[1].color = IM_COL32(64, 230, 77, 255);
    axes[1].label = "Y";
    axes[2].color = IM_COL32(77, 115, 242, 255);
    axes[2].label = "Z";

    if (axes[0].depth > axes[1].depth) {
        std::swap(axes[0], axes[1]);
    }
    if (axes[1].depth > axes[2].depth) {
        std::swap(axes[1], axes[2]);
    }
    if (axes[0].depth > axes[1].depth) {
        std::swap(axes[0], axes[1]);
    }

    draw->AddCircleFilled(center, kRadius, IM_COL32(20, 20, 24, 160), 32);
    draw->AddCircle(center, kRadius, IM_COL32(255, 255, 255, 40), 32, 1.0f);

    for (const AxisTip& axis : axes) {
        draw->AddLine(center, axis.tip, axis.color, 2.0f);
        draw->AddCircleFilled(axis.tip, 7.0f, axis.color, 16);
        const ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        draw->AddText(
            ImVec2(axis.tip.x - textSize.x * 0.5f, axis.tip.y - textSize.y * 0.5f),
            IM_COL32(255, 255, 255, 255),
            axis.label);
    }
}

} // namespace

struct ViewportPanel::Impl {
    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    GLFWwindow* window = nullptr;
    bool selectionActivated = false;
    bool sceneInteracting = false;
    bool deleteRequested = false;
    bool showColliders = false;
    bool showLiveScene = true;
    bool liveSceneVisible = false;
    bool m_showGameStats = false;
    ImDrawList* m_gameStatsDrawList = nullptr;
    ImVec2 m_gameStatsClipMin{};
    ImVec2 m_gameStatsClipMax{};
    TransformTool tool = TransformTool::Move;
    int gameViewWidth = 0;
    int gameViewHeight = 0;
    int sceneViewWidth = 0;
    int sceneViewHeight = 0;
    std::uintptr_t cameraIcon = 0;
    std::array<std::uintptr_t, 3> lightIcons{};

    struct EditorCamera {
        DVec3 position{-5.12, 3.38, -5.12};
        float yawDegrees = 45.0f;
        float pitchDegrees = -25.0f;
        float moveSpeed = kDefaultSceneCameraSpeed;
        bool looking = false;
        bool draggingPan = false;
        double lastCursorX = 0.0;
        double lastCursorY = 0.0;

        [[nodiscard]] Vec3 forward() const {
            const float pitch = pitchDegrees * kDegToRad;
            const float yaw = yawDegrees * kDegToRad;
            const float cosPitch = std::cos(pitch);
            return normalize(Vec3{
                cosPitch * std::sin(yaw),
                std::sin(pitch),
                cosPitch * std::cos(yaw),
            });
        }

        [[nodiscard]] Vec3 right() const {
            return normalize(cross(Vec3{0.0f, 1.0f, 0.0f}, forward()));
        }
    } editorCamera;

    void updateMouseLook(const ImVec2& imageMin, const ImVec2& imageMax) {
        auto& cam = editorCamera;
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window, &x, &y);
        cam.yawDegrees += static_cast<float>(x - cam.lastCursorX) * 0.25f;
        cam.pitchDegrees = std::clamp(
            cam.pitchDegrees - static_cast<float>(y - cam.lastCursorY) * 0.25f, -89.0f, 89.0f);

        // GLFW cursor coordinates are window-local. ImGui uses the same origin
        // in the editor's single-window setup.
        const auto wrap = [](double value, float minimum, float maximum) {
            constexpr double kWrapInset = 3.0;
            if (maximum - minimum <= 2.0 * kWrapInset) { return value; }
            if (value <= minimum + 1.0) { return maximum - kWrapInset; }
            if (value >= maximum - 1.0) { return minimum + kWrapInset; }
            return value;
        };
        const double wrappedX = wrap(x, imageMin.x, imageMax.x);
        const double wrappedY = wrap(y, imageMin.y, imageMax.y);
        if (wrappedX != x || wrappedY != y) {
            glfwSetCursorPos(window, wrappedX, wrappedY);
            // Use the actual warped position so the jump itself adds no rotation.
            glfwGetCursorPos(window, &x, &y);
        }
        cam.lastCursorX = x;
        cam.lastCursorY = y;
    }

    void focusSelection(const Entity* entity) {
        if (!entity || gizmoDrag.active()) { return; }
        const Vec3 forward = editorCamera.forward();
        editorCamera.position = entity->transform.position - DVec3{forward.x, forward.y, forward.z} * 10.0;
    }

    TransformGizmoDrag gizmoDrag;
    struct DragTarget {
        EntityId id;
        Transform authored;
        Transform visible;
        Vec3 previousRotation;
        bool explicitSelection;
        bool visibleAtStart;
    };
    std::vector<DragTarget> dragTargets;
    std::vector<EntityId> dragTargetIds;
    const std::unordered_set<EntityId>* selection = nullptr;
    std::unordered_set<EntityId> dragSelection;
    bool toggleSelection = false;
    bool gridSnap = false, angleSnap = false;
    double gridStep = 1, angleStep = 15;
    Transform dragPivot;
    Transform gizmoTransform;
    TransformSpace moveSpace = TransformSpace::Global;
    TransformSpace rotateSpace = TransformSpace::Local;
    GizmoHit hoveredHandle;
    EntityId hoveredOwner = kInvalidEntityId;

    TransformSpace space() const {
        return tool == TransformTool::Move ? moveSpace : tool == TransformTool::Rotate ? rotateSpace : TransformSpace::Local;
    }
    Transform pivot(const Entity& entity, const Scene& visibleScene) const {
        Transform result = entity.transform;
        if (!selection || selection->size() < 2) { return result; }
        DVec3 offsets{};
        std::size_t count = 0;
        for (EntityId id : *selection) {
            if (const auto* item = visibleScene.entityById(id)) {
                offsets = offsets + (item->transform.position - entity.transform.position);
                ++count;
            }
        }
        if (count > 0) { result.position = result.position + offsets * (1.0 / count); }
        result.scale = {1, 1, 1};
        return result;
    }

    TransformGizmoInstance gizmo(const Entity& entity, const Scene& visibleScene) const {
        TransformGizmoInstance result;
        result.tool = tool;
        const Transform pose = gizmoDrag.active() ? gizmoTransform : pivot(entity, visibleScene);
        result.origin = toVec3(pose.position - editorCamera.position);
        result.basis = transformGizmoBasis(tool, space(), gizmoDrag.active() ? dragPivot.rotation : pose.rotation);
        result.uniformScaleOnly = selection && selection->size() > 1;
        if (gizmoDrag.active() && gizmoDrag.owner() == entity.id()) {
            result.highlightedHandle = gizmoDrag.handle(); result.dragging = true;
        } else if (hoveredOwner == entity.id()) { result.highlightedHandle = hoveredHandle.handle; }
        return result;
    }
    bool captureTargets(const Scene& visibleScene) {
        dragTargets.clear();
        dragTargetIds.clear();
        dragSelection = *selection;
        bool hasDescendants = false;
        for (EntityId id : dragSelection) {
            const auto* authored = scene->entityById(id);
            if (!authored || !visibleScene.entityById(id)) { return false; }
            hasDescendants |= authored->hasChildren();
        }
        const auto capture = [&](const Entity& item, bool explicitSelection) {
            const auto* visible = visibleScene.entityById(item.id());
            const Transform& start = explicitSelection && visible ? visible->transform : item.transform;
            dragTargets.push_back({item.id(), item.transform, start, start.rotation, explicitSelection, visible != nullptr});
            dragTargetIds.push_back(item.id());
        };
        if (hasDescendants) {
            for (const Entity& item : scene->entities()) {
                const bool selected = dragSelection.contains(item.id());
                EntityId parent = item.parentId();
                while (!selected && parent != kInvalidEntityId && !dragSelection.contains(parent)) {
                    const auto* ancestor = scene->entityById(parent);
                    parent = ancestor ? ancestor->parentId() : kInvalidEntityId;
                }
                if (selected || parent != kInvalidEntityId) { capture(item, selected); }
            }
        } else {
            for (EntityId id : dragSelection) {
                if (const auto* item = scene->entityById(id)) { capture(*item, true); }
            }
        }
        return !dragTargets.empty();
    }

    void applyDrag() {
        const bool group = dragSelection.size() > 1;
        const auto displacement = gizmoTransform.position - dragPivot.position;
        const auto rotation = gizmoDrag.worldRotationDelta();
        double factor = gizmoDrag.scaleFactor();
        if (group && tool == TransformTool::Scale && factor != 1) {
            double minimum = 0;
            double maximum = std::numeric_limits<double>::max();
            for (const auto& target : dragTargets) {
                const auto& scale = target.visible.scale;
                for (float value : {scale.x, scale.y, scale.z}) {
                    const double magnitude = std::max(static_cast<double>(std::abs(value)), static_cast<double>(kGizmoMinimumScale));
                    minimum = std::max(minimum, kGizmoMinimumScale / magnitude);
                    maximum = std::min(maximum, std::numeric_limits<float>::max() / magnitude);
                }
            }
            factor = std::clamp(factor, minimum, maximum);
        }
        Transform parentTransform = sceneDragStart;
        if (tool == TransformTool::Move) { parentTransform.position = gizmoTransform.position; }
        else if (tool == TransformTool::Rotate) { parentTransform.rotation = gizmoTransform.rotation; }
        else { parentTransform.scale = gizmoTransform.scale; }
        const TransformDelta parentDelta(sceneDragStart, parentTransform);
        bool changed = false;
        for (auto& target : dragTargets) {
            auto* entity = scene->entityById(target.id);
            Transform next = target.authored;
            if (!group && !target.explicitSelection) {
                next = parentDelta.apply(target.authored);
            } else if (tool == TransformTool::Move) {
                next.position = target.visible.position + displacement;
            } else {
                if (group) {
                    const auto result = transformGizmoGroupMember(target.visible, dragPivot.position, tool,
                        rotation, factor, target.previousRotation);
                    next.position = result.position;
                    if (tool == TransformTool::Rotate) {
                        next.rotation = result.rotation;
                        target.previousRotation = result.rotation;
                    } else { next.scale = result.scale; }
                } else if (tool == TransformTool::Rotate) { next.rotation = gizmoTransform.rotation; }
                else { next.scale = gizmoTransform.scale; }
            }
            if (!sameTransform(entity->transform, next)) {
                entity->transform = next;
                changed = true;
            }
        }
        if (changed) { scene->markEdited(); }
    }

    Transform sceneDragStart;
    void cancelDrag() {
        if (!gizmoDrag.active()) { return; }
        bool restored = false;
        for (const auto& target : dragTargets) {
            if (auto* entity = scene->entityById(target.id); entity && !sameTransform(entity->transform, target.authored)) {
                entity->transform = target.authored;
                restored = true;
            }
        }
        if (restored) { scene->markEdited(); }
        gizmoDrag.finish();
    }

    void drawScenePanel(int& selectedIndex, const Scene& gameScene, bool readOnly) {
        sceneInteracting = false;
        deleteRequested = false;
        const bool playing = &gameScene != scene;
        if (ImGui::Begin(
                "Scene",
                nullptr,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            if (playing) {
                if (ImGui::Checkbox("Live Game", &showLiveScene)) { cancelDrag(); }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show the running game from the Scene camera.");
                }
            }
            liveSceneVisible = playing && showLiveScene;
            const Scene& visibleScene = liveSceneVisible ? gameScene : *scene;
            const bool canUseGizmo = !readOnly;

            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().AppFocusLost;
            if (!focused) { editorCamera.looking = false; editorCamera.draggingPan = false; }
            const Entity* selected = selectedIndex >= 0 ? scene->entity(static_cast<std::size_t>(selectedIndex)) : nullptr;
            const EntityId selectedId = selected ? selected->id() : kInvalidEntityId;
            if (selected && focused && !readOnly && !gizmoDrag.active() && !editorCamera.looking &&
                !editorCamera.draggingPan && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
                deleteRequested = ImGui::Shortcut(ImGuiKey_Delete);
            }
            const auto visibleIndex = visibleScene.entityIndex(selectedId);
            const Entity* visibleSelection = visibleIndex ? visibleScene.entity(*visibleIndex) : nullptr;
            if (gizmoDrag.active() && (!canUseGizmo || !visibleSelection || visibleSelection->id() != gizmoDrag.owner() ||
                !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().AppFocusLost)) { cancelDrag(); }
            hoveredHandle = {}; hoveredOwner = kInvalidEntityId;
            ImGui::BeginDisabled(!canUseGizmo || gizmoDrag.active());
            const TransformTool tools[3] = {TransformTool::Move, TransformTool::Rotate, TransformTool::Scale};
            const char* labels[3] = {"Move (W)", "Rotate (R)", "Scale (T)"};
            for (int i = 0; i < 3; ++i) {
                if (i != 0) { ImGui::SameLine(); }
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(tool == tools[i] ? ImGuiCol_ButtonActive : ImGuiCol_Button));
                if (ImGui::Button(labels[i])) { tool = tools[i]; }
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            int coordinateSpace = space() == TransformSpace::Global ? 0 : 1;
            ImGui::BeginDisabled(tool == TransformTool::Scale);
            if (ImGui::Combo("##TransformSpace", &coordinateSpace, "Global\0Local\0")) {
                auto& preference = tool == TransformTool::Move ? moveSpace : rotateSpace;
                preference = coordinateSpace == 0 ? TransformSpace::Global : TransformSpace::Local;
            }
            ImGui::EndDisabled();
            if (tool == TransformTool::Scale && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Scale uses local axes; the center handle scales uniformly.");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(95);
            ImGui::SliderFloat("Camera Speed", &editorCamera.moveSpeed, kMinSceneCameraSpeed, kMaxSceneCameraSpeed,
                "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scene camera movement speed. Hold Ctrl while right-dragging and scroll to adjust.");
            }
            ImGui::BeginDisabled(!canUseGizmo || gizmoDrag.active());
            ImGui::Checkbox("Grid Snap", &gridSnap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85);
            const double minimumStep = 0.0001, maximumGrid = 1e12, maximumAngle = 360;
            ImGui::DragScalar("##GridStep", ImGuiDataType_Double, &gridStep, 0.1f, &minimumStep, &maximumGrid,
                "%.4g units", ImGuiSliderFlags_AlwaysClamp);
            if (!std::isfinite(gridStep) || gridStep <= 0) { gridStep = 1; }
            ImGui::SameLine();
            ImGui::Checkbox("Angle Snap", &angleSnap);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(85);
            ImGui::DragScalar("##AngleStep", ImGuiDataType_Double, &angleStep, 0.25f, &minimumStep, &maximumAngle,
                "%.4g deg", ImGuiSliderFlags_AlwaysClamp);
            if (!std::isfinite(angleStep) || angleStep <= 0) { angleStep = 15; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox("Show Colliders", &showColliders);
            ImGui::SameLine();
            if (ImGui::Button("Focus (F)")) { focusSelection(visibleSelection); }
            const ImVec2 sceneSize = ImGui::GetContentRegionAvail();
            const int sceneWidth = static_cast<int>(sceneSize.x);
            const int sceneHeight = static_cast<int>(sceneSize.y);
            sceneViewWidth = sceneWidth;
            sceneViewHeight = sceneHeight;
            if (sceneWidth >= 1 && sceneHeight >= 1) {
                renderer->resizeSceneView(sceneWidth, sceneHeight);
                const std::uintptr_t sceneTexture = renderer->sceneColorTexture();
                if (sceneTexture != 0) {
                    ImGui::Image(
                        ImTextureRef((ImTextureID)sceneTexture),
                        ImVec2(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight)));
                    const ImVec2 imageMin = ImGui::GetItemRectMin();
                    const ImVec2 imageMax = ImGui::GetItemRectMax();

                    // Capture mouse over the scene image so LMB pick / camera work reliably.
                    ImGui::SetCursorScreenPos(imageMin);
                    ImGui::InvisibleButton(
                        "##SceneViewInput",
                        ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y));
                    const bool hovered = ImGui::IsItemHovered();
                    const bool leftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                    ImGuiIO& io = ImGui::GetIO();
                    auto& cam = editorCamera;

                    drawSceneOrientationGizmo(
                        ImGui::GetWindowDrawList(),
                        imageMin,
                        imageMax,
                        cam.yawDegrees,
                        cam.pitchDegrees);

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        ImGui::SetWindowFocus();
                        cam.looking = !gizmoDrag.active();
                        if (cam.looking) {
                            glfwGetCursorPos(window, &cam.lastCursorX, &cam.lastCursorY);
                        }
                    }
                    if (hovered && !gizmoDrag.active() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                        cam.draggingPan = true;
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        cam.looking = false;
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                        cam.draggingPan = false;
                    }

                    if (cam.looking) {
                        updateMouseLook(imageMin, imageMax);
                    }

                    const Vec3 forward = cam.forward();
                    const Vec3 right = cam.right();
                    const Vec3 up = cross(forward, right);

                    if (!gizmoDrag.active() && io.MouseWheel != 0.0f) {
                        if (cam.looking && io.KeyCtrl) {
                            cam.moveSpeed = adjustSceneCameraSpeed(cam.moveSpeed, io.MouseWheel);
                        } else if (hovered) {
                            cam.position = cam.position + forward * (io.MouseWheel * cam.moveSpeed * 0.25f);
                        }
                    }

                    if (cam.draggingPan) {
                        const float panScale = cam.moveSpeed * 0.002f;
                        cam.position = cam.position + right * (-io.MouseDelta.x * panScale) +
                            up * (io.MouseDelta.y * panScale);
                    }

                    if (hovered && !readOnly && !cam.looking && !cam.draggingPan && !gizmoDrag.active() && !io.WantTextInput &&
                        !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && !io.KeyShift &&
                        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()) {
                        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) { focusSelection(visibleSelection); }
                        if (canUseGizmo) {
                            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { tool = TransformTool::Move; }
                            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { tool = TransformTool::Rotate; }
                            if (ImGui::IsKeyPressed(ImGuiKey_T, false)) { tool = TransformTool::Scale; }
                        }
                    }
                    if (cam.looking && !gizmoDrag.active() && !io.WantTextInput) {
                        Vec3 move{};
                        if (ImGui::IsKeyDown(ImGuiKey_W)) {
                            move = move + forward;
                        }
                        if (ImGui::IsKeyDown(ImGuiKey_S)) {
                            move = move - forward;
                        }
                        if (ImGui::IsKeyDown(ImGuiKey_A)) {
                            move = move - right;
                        }
                        if (ImGui::IsKeyDown(ImGuiKey_D)) {
                            move = move + right;
                        }
                        if (length(move) > 0.0001f) {
                            const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
                            cam.position = cam.position + normalize(move) * (cam.moveSpeed * dt);
                        }
                    }

                    const float viewW = static_cast<float>(sceneWidth);
                    const float viewH = static_cast<float>(sceneHeight);
                    const Mat4 view =
                        lookAt(Vec3{}, forward, Vec3{0.0f, 1.0f, 0.0f});
                    const Mat4 projection = perspective(60.0f, viewW / viewH, 0.1f, 1000.0f);
                    const EntityId clickedLight = drawEntityIcons(
                        ImGui::GetWindowDrawList(),
                        imageMin,
                        view,
                        projection,
                        viewW,
                        viewH,
                        visibleScene,
                        visibleIndex ? static_cast<int>(*visibleIndex) : -1,
                        cameraIcon, lightIcons, cam.position, leftClicked ? &io.MousePos : nullptr);

                    const float mouseX = io.MousePos.x - imageMin.x;
                    const float mouseY = io.MousePos.y - imageMin.y;
                    const Ray mouseRay = unprojectRay(mouseX, mouseY, viewW, viewH, view, projection);
                    const bool cameraBusy = cam.looking || cam.draggingPan;

                    const bool sceneFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.AppFocusLost;
                    if (gizmoDrag.active()) {
                        if (!canUseGizmo || !sceneFocused || !visibleSelection || visibleSelection->id() != gizmoDrag.owner() ||
                            *selection != dragSelection || std::any_of(dragTargets.begin(), dragTargets.end(), [&](const DragTarget& target) {
                                return !scene->entityById(target.id) || (target.visibleAtStart && !visibleScene.entityById(target.id));
                            }) || !gizmoDrag.viewportMatches(viewW, viewH) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                            cancelDrag();
                        } else {
                            if (gizmoDrag.update(gizmoTransform, mouseX, mouseY)) {
                                applyDrag();
                            }
                            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { gizmoDrag.finish(); }
                        }
                    }
                    if (!readOnly && !cameraBusy && sceneFocused) {
                        if (canUseGizmo && visibleSelection && hovered) {
                            hoveredOwner = visibleSelection->id();
                            hoveredHandle = pickTransformGizmo(gizmo(*visibleSelection, visibleScene), view, projection, viewW, viewH, mouseX, mouseY);
                        }
                        if (!gizmoDrag.active() && leftClicked) {
                            if (selected && visibleSelection && hoveredHandle.handle >= 0) {
                                dragPivot = pivot(*visibleSelection, visibleScene);
                                if (captureTargets(visibleScene) && gizmoDrag.begin(selected->id(), dragPivot, tool, space(), hoveredHandle,
                                        view, projection, viewW, viewH, mouseX, mouseY, cam.position,
                                        {gridSnap ? gridStep : 0, angleSnap ? angleStep : 0})) {
                                    sceneDragStart = selected->transform;
                                    gizmoTransform = dragPivot;
                                }
                            } else {
                                EntityId bestEntity = clickedLight;
                                float bestT = 1.0e30f;
                                if (clickedLight == kInvalidEntityId) {
                                    for (const Entity& entity : visibleScene.entities()) {
                                        float t = 0.0f;
                                        bool hit = false;
                                        if (entity.hasMesh) {
                                            const Mat4 world =
                                                matrixFromTransform(entity.transform, cam.position);
                                            Vec3 minimum{-0.5f, -0.5f, -0.5f};
                                            Vec3 maximum{0.5f, 0.5f, 0.5f};
                                            if (entity.mesh.meshPath.empty() ||
                                                renderer->meshBounds(entity.mesh.meshPath, minimum, maximum, entity.mesh.meshNode)) {
                                                hit = intersectRayBounds(mouseRay, world, t, minimum, maximum);
                                            }
                                        } else if (entity.hasCamera) {
                                            const Mat4 world =
                                                cameraPickWorld(toVec3(entity.transform.position - cam.position));
                                            hit = intersectRayBounds(mouseRay, world, t);
                                        }
                                        if (hit && t < bestT) {
                                            bestT = t;
                                            bestEntity = entity.id();
                                        }
                                    }
                                }
                                selectionActivated = true;
                                toggleSelection = io.KeyCtrl;
                                const auto authoredIndex = scene->entityIndex(scene->selectionRootId(bestEntity));
                                selectedIndex = authoredIndex ? static_cast<int>(*authoredIndex) : -1;
                            }
                        }
                    }
                    sceneInteracting = cameraBusy || gizmoDrag.active() ||
                        (hovered && (io.MouseWheel != 0.0f || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
                            ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                            ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                            ImGui::IsMouseDown(ImGuiMouseButton_Middle)));
                } else {
                    cancelDrag();
                    editorCamera.looking = false; editorCamera.draggingPan = false;
                    ImGui::TextUnformatted("Scene view unavailable");
                }
            } else {
                cancelDrag();
                editorCamera.looking = false; editorCamera.draggingPan = false;
                ImGui::TextUnformatted("Scene view unavailable");
            }
        } else {
            sceneViewWidth = 0;
            sceneViewHeight = 0;
            editorCamera.looking = false;
            editorCamera.draggingPan = false;
            cancelDrag();
        }
        ImGui::End();
    }

    void drawGamePanel(const Scene& gameScene, bool focusGameView, GameUi* ui, const Input* input) {
        m_gameStatsDrawList = nullptr;
        ImVec2 imageOrigin{};
        bool hovered = false;
        bool focused = false;
        if (focusGameView) {
            ImGui::SetNextWindowFocus();
        }
        if (ImGui::Begin(
                "Game",
                nullptr,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::Checkbox("Stats", &m_showGameStats);
            const ImVec2 gameSize = ImGui::GetContentRegionAvail();
            const int gameWidth = static_cast<int>(gameSize.x);
            const int gameHeight = static_cast<int>(gameSize.y);
            gameViewWidth = gameWidth;
            gameViewHeight = gameHeight;
            if (gameScene.mainCameraIndex() < 0) {
                ImGui::TextUnformatted("No camera to render the game view.");
            } else if (gameWidth >= 1 && gameHeight >= 1) {
                renderer->resizeGameView(gameWidth, gameHeight);
                const std::uintptr_t gameTexture = renderer->gameColorTexture();
                if (gameTexture != 0) {
                    ImGui::Image(
                        ImTextureRef((ImTextureID)gameTexture),
                        ImVec2(static_cast<float>(gameWidth), static_cast<float>(gameHeight)));
                    imageOrigin = ImGui::GetItemRectMin();
                    if (m_showGameStats) {
                        m_gameStatsDrawList = ImGui::GetWindowDrawList();
                        const ImVec2 imageEnd = ImGui::GetItemRectMax();
                        const ImVec2 clipMin = m_gameStatsDrawList->GetClipRectMin();
                        const ImVec2 clipMax = m_gameStatsDrawList->GetClipRectMax();
                        m_gameStatsClipMin = {std::max(imageOrigin.x, clipMin.x), std::max(imageOrigin.y, clipMin.y)};
                        m_gameStatsClipMax = {std::min(imageEnd.x, clipMax.x), std::min(imageEnd.y, clipMax.y)};
                    }
                    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                        int windowX = 0;
                        int windowY = 0;
                        glfwGetWindowPos(window, &windowX, &windowY);
                        imageOrigin.x -= static_cast<float>(windowX);
                        imageOrigin.y -= static_cast<float>(windowY);
                    }
                    hovered = ImGui::IsItemHovered();
                    focused = ImGui::IsWindowFocused() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
                    if (ui) { ui->setViewport(gameWidth, gameHeight); }
                } else {
                    ImGui::TextUnformatted("Game view unavailable");
                }
            } else {
                ImGui::TextUnformatted("Game view unavailable");
            }
        } else {
            gameViewWidth = 0;
            gameViewHeight = 0;
        }
        ImGui::End();
        if (ui && input) {
            const bool windowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
            ui->processInput(*input, imageOrigin.x, imageOrigin.y, 1.0f, 1.0f,
                windowFocused && hovered, windowFocused && (hovered || focused));
        }
    }
};

ViewportPanel::ViewportPanel(Scene& scene, Renderer& renderer, GLFWwindow& window)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
    m_impl->renderer = &renderer;
    m_impl->window = &window;
    m_impl->cameraIcon = loadIconTexture(renderer, "Icons/video_camera_ic.png");
    m_impl->lightIcons = {
        loadIconTexture(renderer, "Icons/sun_ic.png"),
        loadIconTexture(renderer, "Icons/light_ic.png"),
        loadIconTexture(renderer, "Icons/spotlight_ic.png"),
    };
}

ViewportPanel::~ViewportPanel() {
    if (m_impl->renderer != nullptr) {
        destroyIconTexture(*m_impl->renderer, m_impl->cameraIcon);
        for (auto& icon : m_impl->lightIcons) {
            destroyIconTexture(*m_impl->renderer, icon);
        }
    }
}

void ViewportPanel::onImGuiRender(int& selectedIndex, const std::unordered_set<EntityId>& selection, const Scene& gameScene, bool sceneReadOnly, bool focusGameView,
    GameUi* ui, const Input* input) {
    m_impl->selectionActivated = false;
    m_impl->liveSceneVisible = false;
    m_impl->selection = &selection;
    m_impl->drawScenePanel(selectedIndex, gameScene, sceneReadOnly);
    m_impl->drawGamePanel(gameScene, focusGameView, ui, input);
}

bool ViewportPanel::gameStatsVisible() const { return m_impl->m_showGameStats; }

void ViewportPanel::drawGameStats() {
    auto& panel = *m_impl;
    auto* draw = std::exchange(panel.m_gameStatsDrawList, nullptr);
    if (!draw || panel.m_gameStatsClipMin.x >= panel.m_gameStatsClipMax.x ||
        panel.m_gameStatsClipMin.y >= panel.m_gameStatsClipMax.y) {
        return;
    }

    const auto stats = panel.renderer->gameViewStats();
    char gpuTime[32] = "N/A";
    if (stats.gpuFrameTimeMs) {
        std::snprintf(gpuTime, sizeof(gpuTime), "%.2f ms", *stats.gpuFrameTimeMs);
    }
    const auto timing = [](std::optional<double> value) {
        char result[24] = "N/A";
        if (value) { std::snprintf(result, sizeof(result), "%.2f ms", *value); }
        return std::string(result);
    };
    char text[512];
    std::snprintf(text, sizeof(text),
        "Global Frametime  %.2f ms\nGPU Frametime  %s\n"
        "CPU Scene Sync  %.2f ms\nCPU Mesh Submission  %.2f ms\n"
        "GPU Shadows  %s\nGPU Opaque  %s\nGPU Transmission  %s\nGPU Transparent  %s\nGPU Post  %s\n"
        "Draw Calls  %" PRIu64 "\nSetPass Calls  %" PRIu64 "\nTriangles  %" PRIu64 "\nVertices  %" PRIu64,
        static_cast<double>(ImGui::GetIO().DeltaTime) * 1000.0, gpuTime,
        stats.cpuSceneSyncMs, stats.cpuMeshSubmitMs, timing(stats.gpuShadowMs).c_str(), timing(stats.gpuOpaqueMs).c_str(),
        timing(stats.gpuTransmissionMs).c_str(), timing(stats.gpuTransparentMs).c_str(), timing(stats.gpuPostMs).c_str(),
        stats.drawCalls, stats.setPassCalls, stats.triangles, stats.vertices);
    const ImVec2 size = ImGui::CalcTextSize(text);
    constexpr float kPadding = 8.0f;
    const ImVec2 origin{
        std::max(panel.m_gameStatsClipMin.x + kPadding, panel.m_gameStatsClipMax.x - size.x - 3.0f * kPadding),
        panel.m_gameStatsClipMin.y + kPadding};
    const ImVec2 end{origin.x + size.x + 2.0f * kPadding, origin.y + size.y + 2.0f * kPadding};
    draw->PushClipRect(panel.m_gameStatsClipMin, panel.m_gameStatsClipMax);
    draw->AddRectFilled(origin, end, IM_COL32(16, 18, 22, 225), 4.0f);
    draw->AddText(ImVec2(origin.x + kPadding, origin.y + kPadding), IM_COL32_WHITE, text);
    draw->PopClipRect();
}

TransformGizmoInstance ViewportPanel::gizmo(const Entity& entity, const Scene& visibleScene) const {
    return m_impl->gizmo(entity, visibleScene);
}
std::span<const EntityId> ViewportPanel::transformTargets() const { return m_impl->dragTargetIds; }
bool ViewportPanel::transformActive() const { return m_impl->gizmoDrag.active(); }
bool ViewportPanel::selectionToggle() const { return m_impl->toggleSelection; }

bool ViewportPanel::showingLiveScene() const { return m_impl->liveSceneVisible; }

bool ViewportPanel::selectionActivated() const { return m_impl->selectionActivated; }

bool ViewportPanel::deleteRequested() const { return m_impl->deleteRequested; }

bool ViewportPanel::sceneViewInteracting() const { return m_impl->sceneInteracting; }

void ViewportPanel::resetInteraction() {
    m_impl->cancelDrag();
    m_impl->editorCamera.looking = false;
    m_impl->editorCamera.draggingPan = false;
}

int ViewportPanel::sceneViewWidth() const {
    return m_impl->sceneViewWidth;
}

int ViewportPanel::sceneViewHeight() const {
    return m_impl->sceneViewHeight;
}

int ViewportPanel::gameViewWidth() const {
    return m_impl->gameViewWidth;
}

int ViewportPanel::gameViewHeight() const {
    return m_impl->gameViewHeight;
}

bool ViewportPanel::showColliders() const { return m_impl->showColliders; }

void ViewportPanel::fillSceneCamera(GameViewCamera& out) const {
    out.clearColor[0] = 0.10f;
    out.clearColor[1] = 0.10f;
    out.clearColor[2] = 0.11f;
    out.clearColor[3] = 1.0f;

    const auto& cam = m_impl->editorCamera;
    out.renderOrigin = cam.position;
    const Vec3 forward = cam.forward();
    const Mat4 view = lookAt(Vec3{}, forward, Vec3{0.0f, 1.0f, 0.0f});
    std::memcpy(out.view, view.m, sizeof(out.view));

    const float aspect =
        (m_impl->sceneViewHeight > 0)
            ? static_cast<float>(m_impl->sceneViewWidth) /
                  static_cast<float>(m_impl->sceneViewHeight)
            : 1.0f;
    const Mat4 projection = perspective(60.0f, aspect, 0.1f, 1000.0f);
    std::memcpy(out.projection, projection.m, sizeof(out.projection));
}

} // namespace sat
