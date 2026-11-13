#include "ViewportPanel.hpp"

#include "EnginePaths.hpp"
#include "Math.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include "TransformGizmo.hpp"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
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
    bool showColliders = false;
    bool showLiveScene = true;
    bool liveSceneVisible = false;
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
        float moveSpeed = 8.0f;
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
    Transform authoredDragStart;
    Transform gizmoTransform;
    TransformSpace moveSpace = TransformSpace::Global;
    TransformSpace rotateSpace = TransformSpace::Local;
    GizmoHit hoveredHandle;
    EntityId hoveredOwner = kInvalidEntityId;

    TransformSpace space() const {
        return tool == TransformTool::Move ? moveSpace : tool == TransformTool::Rotate ? rotateSpace : TransformSpace::Local;
    }
    TransformGizmoInstance gizmo(const Entity& entity) const {
        TransformGizmoInstance result;
        result.tool = tool;
        result.origin = toVec3(entity.transform.position - editorCamera.position);
        result.basis = transformGizmoBasis(tool, space(), entity.transform.rotation);
        if (gizmoDrag.active() && gizmoDrag.owner() == entity.id()) {
            result.highlightedHandle = gizmoDrag.handle(); result.dragging = true;
        } else if (hoveredOwner == entity.id()) { result.highlightedHandle = hoveredHandle.handle; }
        return result;
    }
    void setAuthoredTransform(const Transform& transform) {
        Entity* entity = scene->entityById(gizmoDrag.owner());
        if (!entity) { return; }
        switch (tool) {
        case TransformTool::Move:
            if (entity->transform.position == transform.position) { return; }
            entity->transform.position = transform.position;
            break;
        case TransformTool::Rotate:
            if (entity->transform.rotation == transform.rotation) { return; }
            entity->transform.rotation = transform.rotation;
            break;
        case TransformTool::Scale:
            if (entity->transform.scale == transform.scale) { return; }
            entity->transform.scale = transform.scale;
            break;
        }
        scene->markEdited();
    }

    void cancelDrag() {
        if (gizmoDrag.active()) {
            setAuthoredTransform(authoredDragStart);
            gizmoDrag.finish();
        }
    }

    void drawScenePanel(int& selectedIndex, const Scene& gameScene, bool readOnly) {
        sceneInteracting = false;
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

                    if (cam.draggingPan) {
                        const float panScale = cam.moveSpeed * 0.002f;
                        cam.position = cam.position + right * (-io.MouseDelta.x * panScale) +
                            up * (io.MouseDelta.y * panScale);
                    }

                    if (hovered && !gizmoDrag.active() && io.MouseWheel != 0.0f) {
                        cam.position = cam.position + forward * (io.MouseWheel * cam.moveSpeed * 0.25f);
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
                            !gizmoDrag.viewportMatches(viewW, viewH) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                            cancelDrag();
                        } else {
                            if (gizmoDrag.update(gizmoTransform, mouseX, mouseY)) {
                                setAuthoredTransform(gizmoTransform);
                            }
                            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { gizmoDrag.finish(); }
                        }
                    }
                    if (!readOnly && !cameraBusy && sceneFocused) {
                        if (canUseGizmo && visibleSelection && hovered) {
                            hoveredOwner = visibleSelection->id();
                            hoveredHandle = pickTransformGizmo(gizmo(*visibleSelection), view, projection, viewW, viewH, mouseX, mouseY);
                        }
                        if (!gizmoDrag.active() && leftClicked) {
                            if (selected && visibleSelection && hoveredHandle.handle >= 0) {
                                if (gizmoDrag.begin(selected->id(), visibleSelection->transform, tool, space(), hoveredHandle,
                                        view, projection, viewW, viewH, mouseX, mouseY, cam.position)) {
                                    authoredDragStart = selected->transform;
                                    gizmoTransform = visibleSelection->transform;
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
                                                renderer->meshBounds(entity.mesh.meshPath, minimum, maximum)) {
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
                                const auto authoredIndex = scene->entityIndex(bestEntity);
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

    void drawGamePanel(const Scene& gameScene, bool focusGameView) {
        if (focusGameView) {
            ImGui::SetNextWindowFocus();
        }
        if (ImGui::Begin(
                "Game",
                nullptr,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
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

void ViewportPanel::onImGuiRender(int& selectedIndex, const Scene& gameScene, bool sceneReadOnly, bool focusGameView) {
    m_impl->selectionActivated = false;
    m_impl->liveSceneVisible = false;
    m_impl->drawScenePanel(selectedIndex, gameScene, sceneReadOnly);
    m_impl->drawGamePanel(gameScene, focusGameView);
}

TransformGizmoInstance ViewportPanel::gizmo(const Entity& entity) const { return m_impl->gizmo(entity); }

bool ViewportPanel::showingLiveScene() const { return m_impl->liveSceneVisible; }

bool ViewportPanel::selectionActivated() const { return m_impl->selectionActivated; }

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
