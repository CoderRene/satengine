#include "ViewportPanel.hpp"

#include "EnginePaths.hpp"
#include "Math.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"

#include <imgui.h>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace sat {
namespace {

constexpr float kCameraIconSize = 32.0f;

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

void drawCameraIcons(
    ImDrawList* draw,
    const ImVec2& imageMin,
    const Mat4& view,
    const Mat4& projection,
    float viewW,
    float viewH,
    const Scene& scene,
    int selectedIndex,
    std::uintptr_t cameraIcon) {
    if (draw == nullptr || cameraIcon == 0) {
        return;
    }

    const ImTextureRef iconTex((ImTextureID)cameraIcon);
    const auto& entities = scene.entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!entities[i].hasCamera) {
            continue;
        }

        float sx = 0.0f;
        float sy = 0.0f;
        if (!projectWorldToScreen(
                entities[i].transform.position,
                view,
                projection,
                viewW,
                viewH,
                sx,
                sy)) {
            continue;
        }

        const ImVec2 min(
            imageMin.x + sx - kCameraIconSize * 0.5f,
            imageMin.y + sy - kCameraIconSize * 0.5f);
        const ImVec2 max(min.x + kCameraIconSize, min.y + kCameraIconSize);
        draw->AddImage(iconTex, min, max);

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
}

Mat4 cameraPickWorld(const Vec3& position) {
    return translationMatrix(position) *
        scalingMatrix(Vec3{kCameraPickSize, kCameraPickSize, kCameraPickSize});
}

float distancePointToSegment2D(
    float px, float py, float ax, float ay, float bx, float by) {
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float abLenSq = abx * abx + aby * aby;
    float t = 0.0f;
    if (abLenSq > 0.000001f) {
        t = (apx * abx + apy * aby) / abLenSq;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    const float cx = ax + abx * t;
    const float cy = ay + aby * t;
    const float dx = px - cx;
    const float dy = py - cy;
    return std::sqrt(dx * dx + dy * dy);
}

bool intersectRayUnitCube(const Ray& worldRay, const Mat4& world, float& outT) {
    const Mat4 invWorld = inverse(world);
    const Vec3 origin = transformPoint(invWorld, worldRay.origin);
    // Keep direction unnormalized so slab t matches the world-ray parameter.
    const Vec3 direction = transformDirection(invWorld, worldRay.direction);

    float tMin = 0.0f;
    float tMax = 1.0e30f;
    const float bounds[3][2] = {{-0.5f, 0.5f}, {-0.5f, 0.5f}, {-0.5f, 0.5f}};
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

bool intersectRayPlane(
    const Ray& ray, const Vec3& planePoint, const Vec3& planeNormal, Vec3& outHit) {
    const float denom = dot(planeNormal, ray.direction);
    if (std::fabs(denom) < 0.000001f) {
        return false;
    }
    const float t = dot(planePoint - ray.origin, planeNormal) / denom;
    if (t < 0.0f) {
        return false;
    }
    outHit = ray.origin + ray.direction * t;
    return true;
}

int hitTranslateGizmoAxis(
    float mouseX,
    float mouseY,
    const Vec3& origin,
    const Mat4& view,
    const Mat4& projection,
    float viewWidth,
    float viewHeight) {
    constexpr float kHitPixels = 12.0f;
    const Vec3 axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    int bestAxis = -1;
    float bestDist = kHitPixels;
    for (int i = 0; i < 3; ++i) {
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        if (!projectWorldToScreen(origin, view, projection, viewWidth, viewHeight, x0, y0)) {
            continue;
        }
        const Vec3 tip = origin + axes[i] * kTranslateGizmoAxisLength;
        if (!projectWorldToScreen(tip, view, projection, viewWidth, viewHeight, x1, y1)) {
            continue;
        }
        const float dist = distancePointToSegment2D(mouseX, mouseY, x0, y0, x1, y1);
        if (dist < bestDist) {
            bestDist = dist;
            bestAxis = i;
        }
    }
    return bestAxis;
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
    int gameViewWidth = 0;
    int gameViewHeight = 0;
    int sceneViewWidth = 0;
    int sceneViewHeight = 0;
    std::uintptr_t cameraIcon = 0;

    struct EditorCamera {
        Vec3 position{-5.12f, 3.38f, -5.12f};
        float yawDegrees = 45.0f;
        float pitchDegrees = -25.0f;
        float moveSpeed = 8.0f;
        bool looking = false;
        bool draggingPan = false;

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

    struct GizmoDrag {
        bool active = false;
        int entityIndex = -1;
        int axis = -1;
        Vec3 startPosition{};
        float startAxisOffset = 0.0f;
    } gizmoDrag;

    void drawScenePanel(int& selectedIndex, bool playMode) {
        if (ImGui::Begin(
                "Scene",
                nullptr,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
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

                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        cam.looking = true;
                    }
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                        cam.draggingPan = true;
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        cam.looking = false;
                    }
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                        cam.draggingPan = false;
                    }

                    if (cam.looking) {
                        cam.yawDegrees += io.MouseDelta.x * 0.25f;
                        cam.pitchDegrees -= io.MouseDelta.y * 0.25f;
                        if (cam.pitchDegrees > 89.0f) {
                            cam.pitchDegrees = 89.0f;
                        }
                        if (cam.pitchDegrees < -89.0f) {
                            cam.pitchDegrees = -89.0f;
                        }
                    }

                    const Vec3 forward = cam.forward();
                    const Vec3 right = cam.right();
                    const Vec3 up = cross(forward, right);

                    if (cam.draggingPan) {
                        const float panScale = cam.moveSpeed * 0.002f;
                        cam.position = cam.position + right * (-io.MouseDelta.x * panScale) +
                            up * (io.MouseDelta.y * panScale);
                    }

                    if (hovered && io.MouseWheel != 0.0f) {
                        cam.position = cam.position + forward * (io.MouseWheel * cam.moveSpeed * 0.25f);
                    }

                    if (hovered) {
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
                        lookAt(cam.position, cam.position + forward, Vec3{0.0f, 1.0f, 0.0f});
                    const Mat4 projection = perspective(60.0f, viewW / viewH, 0.1f, 1000.0f);
                    drawCameraIcons(
                        ImGui::GetWindowDrawList(),
                        imageMin,
                        view,
                        projection,
                        viewW,
                        viewH,
                        *scene,
                        selectedIndex,
                        cameraIcon);

                    const float mouseX = io.MousePos.x - imageMin.x;
                    const float mouseY = io.MousePos.y - imageMin.y;
                    const Ray mouseRay = unprojectRay(mouseX, mouseY, viewW, viewH, view, projection);
                    const bool cameraBusy = cam.looking || cam.draggingPan;

                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        gizmoDrag.active = false;
                    }

                    if (!playMode && !cameraBusy) {
                        auto& drag = gizmoDrag;
                        if (drag.active && drag.entityIndex >= 0 && drag.axis >= 0) {
                            Entity* entity =
                                scene->entity(static_cast<std::size_t>(drag.entityIndex));
                            if (entity != nullptr) {
                                const Vec3 axes[3] = {
                                    {1.0f, 0.0f, 0.0f},
                                    {0.0f, 1.0f, 0.0f},
                                    {0.0f, 0.0f, 1.0f},
                                };
                                const Vec3 axis = axes[drag.axis];
                                Vec3 planeNormal = cross(axis, cross(forward, axis));
                                if (length(planeNormal) < 0.0001f) {
                                    planeNormal = cross(axis, right);
                                }
                                planeNormal = normalize(planeNormal);
                                Vec3 hit{};
                                if (intersectRayPlane(
                                        mouseRay, drag.startPosition, planeNormal, hit)) {
                                    const float offset = dot(hit - drag.startPosition, axis);
                                    entity->transform.position =
                                        drag.startPosition +
                                        axis * (offset - drag.startAxisOffset);
                                }
                            }
                        } else if (leftClicked) {
                            int gizmoAxis = -1;
                            if (selectedIndex >= 0) {
                                Entity* selectedEntity =
                                    scene->entity(static_cast<std::size_t>(selectedIndex));
                                if (selectedEntity != nullptr) {
                                    gizmoAxis = hitTranslateGizmoAxis(
                                        mouseX,
                                        mouseY,
                                        selectedEntity->transform.position,
                                        view,
                                        projection,
                                        viewW,
                                        viewH);
                                }
                            }

                            if (gizmoAxis >= 0 && selectedIndex >= 0) {
                                Entity* entity =
                                    scene->entity(static_cast<std::size_t>(selectedIndex));
                                if (entity != nullptr) {
                                    const Vec3 axes[3] = {
                                        {1.0f, 0.0f, 0.0f},
                                        {0.0f, 1.0f, 0.0f},
                                        {0.0f, 0.0f, 1.0f},
                                    };
                                    const Vec3 axis = axes[gizmoAxis];
                                    Vec3 planeNormal = cross(axis, cross(forward, axis));
                                    if (length(planeNormal) < 0.0001f) {
                                        planeNormal = cross(axis, right);
                                    }
                                    planeNormal = normalize(planeNormal);
                                    Vec3 hit{};
                                    drag.active = true;
                                    drag.entityIndex = selectedIndex;
                                    drag.axis = gizmoAxis;
                                    drag.startPosition = entity->transform.position;
                                    drag.startAxisOffset = 0.0f;
                                    if (intersectRayPlane(
                                            mouseRay, drag.startPosition, planeNormal, hit)) {
                                        drag.startAxisOffset =
                                            dot(hit - drag.startPosition, axis);
                                    }
                                }
                            } else {
                                int bestEntity = -1;
                                float bestT = 1.0e30f;
                                const auto& entities = scene->entities();
                                for (std::size_t i = 0; i < entities.size(); ++i) {
                                    float t = 0.0f;
                                    bool hit = false;
                                    if (entities[i].hasMesh) {
                                        const Mat4 world =
                                            matrixFromTransform(entities[i].transform);
                                        hit = intersectRayUnitCube(mouseRay, world, t);
                                    } else if (entities[i].hasCamera) {
                                        const Mat4 world =
                                            cameraPickWorld(entities[i].transform.position);
                                        hit = intersectRayUnitCube(mouseRay, world, t);
                                    }
                                    if (hit && t < bestT) {
                                        bestT = t;
                                        bestEntity = static_cast<int>(i);
                                    }
                                }
                                selectedIndex = bestEntity;
                            }
                        }
                    }
                } else {
                    ImGui::TextUnformatted("Scene view unavailable");
                }
            } else {
                ImGui::TextUnformatted("Scene view unavailable");
            }
        } else {
            sceneViewWidth = 0;
            sceneViewHeight = 0;
            editorCamera.looking = false;
            editorCamera.draggingPan = false;
            gizmoDrag.active = false;
        }
        ImGui::End();
    }

    void drawGamePanel(bool focusGameView) {
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
            if (scene->mainCameraIndex() < 0) {
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

ViewportPanel::ViewportPanel(Scene& scene, Renderer& renderer)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
    m_impl->renderer = &renderer;
    m_impl->cameraIcon = loadIconTexture(renderer, "Icons/video_camera_ic.png");
}

ViewportPanel::~ViewportPanel() {
    if (m_impl->renderer != nullptr) {
        destroyIconTexture(*m_impl->renderer, m_impl->cameraIcon);
    }
}

void ViewportPanel::onImGuiRender(int& selectedIndex, bool playMode, bool focusGameView) {
    m_impl->drawScenePanel(selectedIndex, playMode);
    m_impl->drawGamePanel(focusGameView);
}

void ViewportPanel::resetInteraction() {
    m_impl->gizmoDrag.active = false;
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

void ViewportPanel::fillSceneCamera(GameViewCamera& out) const {
    out.clearColor[0] = 0.10f;
    out.clearColor[1] = 0.10f;
    out.clearColor[2] = 0.11f;
    out.clearColor[3] = 1.0f;

    const auto& cam = m_impl->editorCamera;
    const Vec3 forward = cam.forward();
    const Mat4 view = lookAt(cam.position, cam.position + forward, Vec3{0.0f, 1.0f, 0.0f});
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
