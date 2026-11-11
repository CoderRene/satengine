#pragma once

#include "Scene.hpp"
#include "SceneDraw.hpp"

namespace sat {
inline constexpr int kGizmoRingSegments = 64;
inline constexpr float kGizmoTubeRadius = 0.018f;
inline constexpr float kGizmoScaleTipHalfExtent = 0.07f;
inline constexpr float kGizmoCenterHalfExtent = 0.09f;
inline constexpr float kGizmoMinimumScale = 0.0001f;

struct GizmoHit {
    int handle = -1;
    float ringAngle = 0.0f; // Radians on the handle's local circle.
};

// LH world coordinates, Euler degrees Rz*Ry*Rx. Scale always uses local axes.
[[nodiscard]] Mat4 transformGizmoBasis(TransformTool tool, TransformSpace space, const Vec3& rotationDegrees);
[[nodiscard]] Vec3 transformGizmoAxis(const Mat4& basis, int axis);
// Shared by immutable rendered rings, picking, and rotation drag tangents.
[[nodiscard]] Vec3 transformGizmoRingPoint(int axis, float angleRadians);
[[nodiscard]] Vec3 transformGizmoRingVertex(int axis, int segment);
[[nodiscard]] GizmoHit pickTransformGizmo(const TransformGizmoInstance& gizmo,
    const Mat4& view, const Mat4& projection, float width, float height, float mouseX, float mouseY);
[[nodiscard]] Vec3 gizmoEulerDegrees(const Mat4& rotation, const Vec3& referenceDegrees);
[[nodiscard]] Mat4 gizmoAxisRotation(int axis, double radians);
[[nodiscard]] bool sameTransform(const Transform& a, const Transform& b);

// Window-thread only. Owns a drag snapshot and frozen camera/basis; no borrowed
// entities and no allocation. Scene access resolves the stable owner ID each time.
class TransformGizmoDrag {
public:
    [[nodiscard]] bool begin(EntityId owner, const Transform& transform, TransformTool tool, TransformSpace space,
        GizmoHit hit, const Mat4& view, const Mat4& projection, float width, float height, float mouseX, float mouseY);
    [[nodiscard]] bool update(Scene& scene, float mouseX, float mouseY);
    [[nodiscard]] bool cancel(Scene& scene);
    void finish() { m_active = false; }
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool viewportMatches(float width, float height) const { return width == m_width && height == m_height; }
    [[nodiscard]] EntityId owner() const { return m_owner; }
    [[nodiscard]] int handle() const { return m_handle; }
    [[nodiscard]] const Transform& startTransform() const { return m_start; }
private:
    bool m_active = false;
    bool m_usePlane = false;
    EntityId m_owner = kInvalidEntityId;
    int m_handle = -1;
    TransformTool m_tool = TransformTool::Move;
    TransformSpace m_space = TransformSpace::Global;
    Transform m_start;
    Transform m_last;
    Mat4 m_view;
    Mat4 m_projection;
    Mat4 m_rotation;
    Vec3 m_origin;
    Vec3 m_axis;
    Vec3 m_planeNormal;
    Vec3 m_radial;
    float m_width = 0;
    float m_height = 0;
    float m_startX = 0;
    float m_startY = 0;
    float m_lastX = 0;
    float m_lastY = 0;
    float m_screenX = 0;
    float m_screenY = 0;
    float m_screenLength = 0;
    double m_startOffset = 0;
    double m_worldLength = 0;
    double m_angle = 0;
};
}
