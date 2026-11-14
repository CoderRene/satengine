#pragma once

#include "Scene.hpp"

#include <string_view>

namespace sat {

enum class TransformTool { Move, Rotate, Scale };
enum class TransformSpace { Global, Local };

struct TransformGizmoInstance {
    TransformTool tool = TransformTool::Move;
    Vec3 origin{};
    Mat4 basis{}; // Rotation only, never includes entity scale or translation.
    int highlightedHandle = -1; // X/Y/Z = 0/1/2; uniform scale = 3.
    bool dragging = false;
};

inline constexpr float kTranslateGizmoAxisLength = 1.25f;
inline constexpr float kTranslateGizmoAxisPixels = 96.0f;

// Uniform world scale for the immutable gizmo mesh. Uses viewport-local pixels,
// column-major LH view/projection and D3D clip Z [0,W]. Returns zero for invalid
// or depth-clipped origins. Shared by rendering and picking; ignores entity scale.
[[nodiscard]] float translateGizmoScale(const Vec3& origin, const Mat4& view,
    const Mat4& projection, float viewportHeight);
inline constexpr float kCameraPickSize = 0.7f;

enum class MeshPrimitive : unsigned char {
    Cube = 0,
};

struct MeshInstance {
    Mat4 world{};
    MeshPrimitive primitive = MeshPrimitive::Cube;
    // Borrowed from scene components, valid through the render submission.
    std::string_view meshPath;
    std::string_view materialPath;
};

struct CameraFrustumInstance {
    Vec3 position{};
    Vec3 rotation{};
    float fieldOfView = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float aspect = 1.0f;
    bool selected = false;
};

struct DebugLine {
    Vec3 a{}, b{}, color{0,1,0};
};

struct LightInstance {
    Vec3 position;
    Vec3 direction;
    Vec3 radiance;
    LightType type = LightType::Directional;
    float range = 0;
    float innerCos = 1;
    float outerCos = 1;
};

[[nodiscard]] LightInstance makeLightInstance(const Entity& entity, DVec3 renderOrigin);
[[nodiscard]] Vec3 ambientRadiance(const LightingSettings& settings);

struct SceneDrawList {
    std::string_view environmentPath;
    float environmentIntensity = 1.0f;
    bool directionalShadows = true;
    int shadowResolution = 2048;
    float shadowDistance = 100.0f;
    int shadowCascadeCount = 4;
    float shadowSplitLambda = 0.75f;
    BloomSettings bloom;
    const LightInstance* lights = nullptr;
    int lightCount = 0;
    Vec3 ambient{0.3f, 0.3f, 0.3f};
    const DebugLine* debugLines = nullptr;
    int debugLineCount = 0;
    Vec3 gridOffset{};
    const MeshInstance* meshes = nullptr;
    int meshCount = 0;
    int selectedMesh = -1;
    const CameraFrustumInstance* frustums = nullptr;
    int frustumCount = 0;
    // Editor previews can show a reference grid without enabling gizmos/frustums.
    bool drawGrid = false;
    bool drawEditorOverlay = false;
    bool showTransformGizmo = false;
    TransformGizmoInstance gizmo;
};

} // namespace sat
