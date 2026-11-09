#include "UiLayer.hpp"

#include "ContentPanel.hpp"
#include "EnginePaths.hpp"
#include "FileDialog.hpp"
#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "Math.hpp"
#include "Project.hpp"
#include "RecentProjects.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "SceneDraw.hpp"
#include "Window.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>

#include <GLFW/glfw3.h>

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

struct UiLayer::Impl {
    GLFWwindow* window = nullptr;
    Renderer* renderer = nullptr;
    bool glfwReady = false;
    Project project;
    Scene scene;
    RecentProjects recent{std::filesystem::path("recent_projects.txt")};
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    std::unique_ptr<ContentPanel> content;
    int gameViewWidth = 0;
    int gameViewHeight = 0;
    int sceneViewWidth = 0;
    int sceneViewHeight = 0;

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

    std::uintptr_t cameraIcon = 0;

    // Play mode is not implemented yet; FPS is only shown while this is true.
    bool playMode = false;
    bool openNewProjectModal = false;
    std::filesystem::path newProjectParent;
    char newProjectName[128] = {};

    bool openProject(const std::filesystem::path& folder) {
        if (!project.open(folder)) {
            recent.remove(folder);
            return false;
        }
        recent.add(project.root());
        return true;
    }
};
static void applySatTheme() {
    ImGui::StyleColorsDark(); // start from a known base

    ImGuiStyle& style = ImGui::GetStyle();

    // Shape
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Spacing
    style.WindowPadding    = ImVec2(8.0f, 8.0f);
    style.FramePadding     = ImVec2(6.0f, 4.0f);
    style.ItemSpacing      = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing    = 16.0f;
    style.ScrollbarSize    = 12.0f;
    style.GrabMinSize      = 10.0f;

    // Borders
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    // Docking
    style.DockingSeparatorSize = 2.0f;

    ImVec4* c = style.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.13f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);

    // Frames / inputs
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);

    // Accent (pick one brand color and reuse it)
    const ImVec4 accent              = ImVec4(0.35f, 0.60f, 0.95f, 1.00f);
    const ImVec4 accentHover         = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
    const ImVec4 accentActive        = ImVec4(0.28f, 0.50f, 0.85f, 1.00f);

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;
    c[ImGuiCol_Button]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = accentHover;
    c[ImGuiCol_ButtonActive]         = accentActive;
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.35f, 0.48f, 1.00f);
    c[ImGuiCol_HeaderActive]         = accentActive;
    c[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TabHovered]           = accentHover;
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_TabDimmed]          = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]  = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

    // Text / borders
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.30f, 0.34f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accentActive;
}

UiLayer::UiLayer(Window& window, Renderer& renderer)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->window = static_cast<GLFWwindow*>(window.nativeHandle());
    m_impl->renderer = &renderer;
    if (m_impl->window == nullptr) {
        std::cerr << "Failed to start UI: window has no native handle\n";
        return;
    }

    m_impl->hierarchy = std::make_unique<HierarchyPanel>(m_impl->scene);
    m_impl->inspector = std::make_unique<InspectorPanel>(m_impl->scene);
    m_impl->content = std::make_unique<ContentPanel>(m_impl->project, renderer);
    m_impl->cameraIcon = loadIconTexture(renderer, "Icons/video_camera_ic.png");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    const char* fontPath = SAT_ENGINE_ASSETS_DIR "/Fonts/JetBrainsMono.ttf";
    if (io.Fonts->AddFontFromFileTTF(fontPath, 16.0f) == nullptr) {
        // Fallback when running from the binary dir (font copied by Game POST_BUILD).
        fontPath = "Fonts/JetBrainsMono.ttf";
        if (io.Fonts->AddFontFromFileTTF(fontPath, 16.0f) == nullptr) {
            std::cerr << "Failed to load UI font: " << fontPath << '\n';
        }
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    applySatTheme();

    if (!ImGui_ImplGlfw_InitForOther(m_impl->window, true)) {
        std::cerr << "Failed to start UI: GLFW backend\n";
        return;
    }
    m_impl->glfwReady = true;
}

UiLayer::~UiLayer() {
    if (m_impl->renderer != nullptr) {
        destroyIconTexture(*m_impl->renderer, m_impl->cameraIcon);
        m_impl->renderer->shutdownImGui();
    }
    if (m_impl->glfwReady) {
        ImGui_ImplGlfw_Shutdown();
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }
}

void UiLayer::beginFrame() {
    if (!m_impl->glfwReady || m_impl->renderer == nullptr || !m_impl->renderer->valid()) {
        return;
    }

    m_impl->renderer->newFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                const std::filesystem::path folder =
                    openFolderDialog(m_impl->window, "Select Parent Folder");
                if (!folder.empty()) {
                    m_impl->newProjectParent = folder;
                    m_impl->newProjectName[0] = '\0';
                    m_impl->openNewProjectModal = true;
                }
            }
            if (ImGui::MenuItem("Open Project...")) {
                const std::filesystem::path folder =
                    openFolderDialog(m_impl->window, "Open Project Folder");
                if (!folder.empty()) {
                    m_impl->openProject(folder);
                }
            }
            if (ImGui::BeginMenu("Recent...")) {
                const auto& recent = m_impl->recent.entries();
                if (recent.empty()) {
                    ImGui::MenuItem("No recent projects", nullptr, false, false);
                } else {
                    for (const std::filesystem::path& path : recent) {
                        const std::string full = path.string();
                        ImGui::PushID(full.c_str());
                        const std::string label = path.filename().string();
                        if (ImGui::MenuItem(label.c_str())) {
                            m_impl->openProject(path);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", full.c_str());
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }

        if (m_impl->playMode) {
            const float fps = ImGui::GetIO().Framerate;
            char fpsText[32] = {};
            std::snprintf(fpsText, sizeof(fpsText), "%.0f FPS", fps);
            const float textWidth = ImGui::CalcTextSize(fpsText).x;
            const float rightPad = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - textWidth - rightPad);
            ImGui::TextUnformatted(fpsText);
        }
        ImGui::EndMenuBar();
    }

    ImGuiID dockspaceId = ImGui::GetID("SatEngineDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f));

    static bool appliedDefaultLayout = false;
    if (!appliedDefaultLayout) {
        appliedDefaultLayout = true;
        const char* ini = ImGui::GetIO().IniFilename;
        bool hasSavedLayout = ini != nullptr && std::filesystem::exists(ini);
        if (!hasSavedLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockMain = dockspaceId;
            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Right, 0.28f, nullptr, &dockMain);
            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(
                dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            ImGui::DockBuilderDockWindow("Scene", dockMain);
            ImGui::DockBuilderDockWindow("Game", dockMain);
            ImGui::DockBuilderDockWindow("Content", dockBottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    ImGui::End();

    if (m_impl->hierarchy) {
        m_impl->hierarchy->onImGuiRender();
    }

    if (ImGui::Begin(
            "Scene",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 sceneSize = ImGui::GetContentRegionAvail();
        const int sceneWidth = static_cast<int>(sceneSize.x);
        const int sceneHeight = static_cast<int>(sceneSize.y);
        m_impl->sceneViewWidth = sceneWidth;
        m_impl->sceneViewHeight = sceneHeight;
        if (sceneWidth >= 1 && sceneHeight >= 1) {
            m_impl->renderer->resizeSceneView(sceneWidth, sceneHeight);
            const std::uintptr_t sceneTexture = m_impl->renderer->sceneColorTexture();
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
                auto& cam = m_impl->editorCamera;

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
                const int selectedForIcons =
                    m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
                drawCameraIcons(
                    ImGui::GetWindowDrawList(),
                    imageMin,
                    view,
                    projection,
                    viewW,
                    viewH,
                    m_impl->scene,
                    selectedForIcons,
                    m_impl->cameraIcon);

                const float mouseX = io.MousePos.x - imageMin.x;
                const float mouseY = io.MousePos.y - imageMin.y;
                const Ray mouseRay = unprojectRay(mouseX, mouseY, viewW, viewH, view, projection);
                const bool cameraBusy = cam.looking || cam.draggingPan;

                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_impl->gizmoDrag.active = false;
                }

                if (!cameraBusy && m_impl->hierarchy) {
                    auto& drag = m_impl->gizmoDrag;
                    if (drag.active && drag.entityIndex >= 0 && drag.axis >= 0) {
                        Entity* entity =
                            m_impl->scene.entity(static_cast<std::size_t>(drag.entityIndex));
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
                        const int selected = m_impl->hierarchy->selectedIndex();
                        int gizmoAxis = -1;
                        if (selected >= 0) {
                            Entity* selectedEntity =
                                m_impl->scene.entity(static_cast<std::size_t>(selected));
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

                        if (gizmoAxis >= 0 && selected >= 0) {
                            Entity* entity =
                                m_impl->scene.entity(static_cast<std::size_t>(selected));
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
                                drag.entityIndex = selected;
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
                            const auto& entities = m_impl->scene.entities();
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
                            m_impl->hierarchy->setSelectedIndex(bestEntity);
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
        m_impl->sceneViewWidth = 0;
        m_impl->sceneViewHeight = 0;
        m_impl->editorCamera.looking = false;
        m_impl->editorCamera.draggingPan = false;
        m_impl->gizmoDrag.active = false;
    }
    ImGui::End();

    if (ImGui::Begin(
            "Game",
            nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 gameSize = ImGui::GetContentRegionAvail();
        const int gameWidth = static_cast<int>(gameSize.x);
        const int gameHeight = static_cast<int>(gameSize.y);
        m_impl->gameViewWidth = gameWidth;
        m_impl->gameViewHeight = gameHeight;
        if (m_impl->scene.mainCameraIndex() < 0) {
            ImGui::TextUnformatted("No camera to render the game view.");
        } else if (gameWidth >= 1 && gameHeight >= 1) {
            m_impl->renderer->resizeGameView(gameWidth, gameHeight);
            const std::uintptr_t gameTexture = m_impl->renderer->gameColorTexture();
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
        m_impl->gameViewWidth = 0;
        m_impl->gameViewHeight = 0;
    }
    ImGui::End();

    if (m_impl->inspector) {
        const int selected =
            m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
        m_impl->inspector->onImGuiRender(selected);
    }

    if (m_impl->content) {
        m_impl->content->onImGuiRender();
    }

    if (m_impl->openNewProjectModal) {
        ImGui::OpenPopup("New Project");
        m_impl->openNewProjectModal = false;
    }

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Parent folder:");
        ImGui::TextWrapped("%s", m_impl->newProjectParent.string().c_str());
        ImGui::Spacing();
        ImGui::InputText("Project name", m_impl->newProjectName, sizeof(m_impl->newProjectName));

        const bool canCreate = m_impl->newProjectName[0] != '\0';
        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
            if (m_impl->project.create(
                    m_impl->newProjectParent, std::string(m_impl->newProjectName))) {
                m_impl->recent.add(m_impl->project.root());
                ImGui::CloseCurrentPopup();
            }
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void UiLayer::endFrame() {
    if (!m_impl->glfwReady || m_impl->renderer == nullptr || !m_impl->renderer->valid()) {
        return;
    }

    GameViewCamera sceneCamera{};
    sceneCamera.clearColor[0] = 0.10f;
    sceneCamera.clearColor[1] = 0.10f;
    sceneCamera.clearColor[2] = 0.11f;
    sceneCamera.clearColor[3] = 1.0f;
    {
        const auto& cam = m_impl->editorCamera;
        const Vec3 forward = cam.forward();
        const Mat4 view = lookAt(cam.position, cam.position + forward, Vec3{0.0f, 1.0f, 0.0f});
        std::memcpy(sceneCamera.view, view.m, sizeof(sceneCamera.view));

        const float aspect =
            (m_impl->sceneViewHeight > 0)
                ? static_cast<float>(m_impl->sceneViewWidth) /
                      static_cast<float>(m_impl->sceneViewHeight)
                : 1.0f;
        const Mat4 projection = perspective(60.0f, aspect, 0.1f, 1000.0f);
        std::memcpy(sceneCamera.projection, projection.m, sizeof(sceneCamera.projection));
    }

    GameViewCamera gameCamera{};
    const int mainCamera = m_impl->scene.mainCameraIndex();
    if (mainCamera >= 0) {
        const Entity* entity =
            m_impl->scene.entity(static_cast<std::size_t>(mainCamera));
        if (entity != nullptr && entity->hasCamera) {
            std::memcpy(gameCamera.clearColor, entity->camera.clearColor, sizeof(gameCamera.clearColor));
            const Mat4 view = viewFromEuler(entity->transform.position, entity->transform.rotation);
            std::memcpy(gameCamera.view, view.m, sizeof(gameCamera.view));

            const float aspect =
                (m_impl->gameViewHeight > 0)
                    ? static_cast<float>(m_impl->gameViewWidth) /
                          static_cast<float>(m_impl->gameViewHeight)
                    : 1.0f;
            const Mat4 projection = perspective(
                entity->camera.fieldOfView,
                aspect,
                entity->camera.nearClip,
                entity->camera.farClip);
            std::memcpy(gameCamera.projection, projection.m, sizeof(gameCamera.projection));
        }
    }

    std::vector<MeshInstance> meshInstances;
    std::vector<CameraFrustumInstance> frustumInstances;
    int selectedMesh = -1;
    const int selectedEntity =
        m_impl->hierarchy ? m_impl->hierarchy->selectedIndex() : -1;
    const float sceneAspect =
        (m_impl->sceneViewHeight > 0)
            ? static_cast<float>(m_impl->sceneViewWidth) /
                  static_cast<float>(m_impl->sceneViewHeight)
            : 1.0f;
    const auto& entities = m_impl->scene.entities();
    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].hasMesh) {
            MeshInstance instance{};
            instance.world = matrixFromTransform(entities[i].transform);
            switch (entities[i].mesh.primitive) {
            case MeshComponent::Primitive::Cube:
                instance.primitive = MeshPrimitive::Cube;
                break;
            }
            if (static_cast<int>(i) == selectedEntity) {
                selectedMesh = static_cast<int>(meshInstances.size());
            }
            meshInstances.push_back(instance);
        }
        if (entities[i].hasCamera) {
            CameraFrustumInstance frustum{};
            frustum.position = entities[i].transform.position;
            frustum.rotation = entities[i].transform.rotation;
            frustum.fieldOfView = entities[i].camera.fieldOfView;
            frustum.nearClip = entities[i].camera.nearClip;
            frustum.farClip = entities[i].camera.farClip;
            frustum.aspect = sceneAspect;
            frustum.selected = (static_cast<int>(i) == selectedEntity);
            frustumInstances.push_back(frustum);
        }
    }

    SceneDrawList sceneDraw{};
    sceneDraw.meshes = meshInstances.empty() ? nullptr : meshInstances.data();
    sceneDraw.meshCount = static_cast<int>(meshInstances.size());
    sceneDraw.selectedMesh = selectedMesh;
    sceneDraw.frustums = frustumInstances.empty() ? nullptr : frustumInstances.data();
    sceneDraw.frustumCount = static_cast<int>(frustumInstances.size());
    sceneDraw.drawEditorOverlay = true;
    if (selectedEntity >= 0) {
        const Entity* selected = m_impl->scene.entity(static_cast<std::size_t>(selectedEntity));
        if (selected != nullptr) {
            sceneDraw.showTranslateGizmo = true;
            sceneDraw.translateGizmoOrigin = selected->transform.position;
        }
    }

    SceneDrawList gameDraw{};
    gameDraw.meshes = sceneDraw.meshes;
    gameDraw.meshCount = sceneDraw.meshCount;
    gameDraw.drawEditorOverlay = false;

    m_impl->renderer->renderSceneView(sceneCamera, sceneDraw);
    if (mainCamera >= 0) {
        m_impl->renderer->renderGameView(gameCamera, gameDraw);
    }
    ImGui::Render();
    m_impl->renderer->drawImGui();
    m_impl->renderer->present();
}

} // namespace sat
