#pragma once

#include "Math.hpp"

#include <string_view>

namespace sat {

inline constexpr float kTranslateGizmoAxisLength = 1.25f;
inline constexpr float kCameraPickSize = 0.7f;

enum class MeshPrimitive : unsigned char {
    Cube = 0,
};

struct MeshInstance {
    Mat4 world{};
    float color[4] = {0.75f, 0.75f, 0.78f, 1.0f};
    MeshPrimitive primitive = MeshPrimitive::Cube;
    // Borrowed from scene components, valid through the render submission.
    std::string_view meshPath;
    std::string_view texturePath;
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

struct SceneDrawList {
    const MeshInstance* meshes = nullptr;
    int meshCount = 0;
    int selectedMesh = -1;
    const CameraFrustumInstance* frustums = nullptr;
    int frustumCount = 0;
    // Editor previews can show a reference grid without enabling gizmos/frustums.
    bool drawGrid = false;
    bool drawEditorOverlay = false;
    bool showTranslateGizmo = false;
    Vec3 translateGizmoOrigin{};
};

} // namespace sat
