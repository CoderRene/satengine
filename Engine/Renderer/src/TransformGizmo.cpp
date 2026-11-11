#include "TransformGizmo.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>

namespace sat {
namespace {
constexpr double kPi = std::numbers::pi_v<double>;
constexpr float kHitPixels = 12.0f;
bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(const Transform& t) {
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.position.z) && finite(t.rotation) && finite(t.scale);
}
bool project(const Vec3& point, const Mat4& viewProjection, float width, float height, float& x, float& y) {
    const float clipX = viewProjection.m[0] * point.x + viewProjection.m[4] * point.y + viewProjection.m[8] * point.z + viewProjection.m[12];
    const float clipY = viewProjection.m[1] * point.x + viewProjection.m[5] * point.y + viewProjection.m[9] * point.z + viewProjection.m[13];
    const float clipZ = viewProjection.m[2] * point.x + viewProjection.m[6] * point.y + viewProjection.m[10] * point.z + viewProjection.m[14];
    const float clipW = viewProjection.m[3] * point.x + viewProjection.m[7] * point.y + viewProjection.m[11] * point.z + viewProjection.m[15];
    if (!std::isfinite(clipX) || !std::isfinite(clipY) || !std::isfinite(clipZ) || !std::isfinite(clipW) ||
        clipW <= 0.0001f || clipZ < 0 || clipZ > clipW) { return false; }
    x = (clipX / clipW * 0.5f + 0.5f) * width;
    y = (0.5f - clipY / clipW * 0.5f) * height;
    return std::isfinite(x) && std::isfinite(y);
}
float segmentDistance(float x, float y, float ax, float ay, float bx, float by, float& fraction) {
    const float dx = bx - ax, dy = by - ay;
    const float lengthSquared = dx * dx + dy * dy;
    fraction = lengthSquared > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / lengthSquared, 0.0f, 1.0f) : 0;
    return std::hypot(x - (ax + fraction * dx), y - (ay + fraction * dy));
}
bool planeHit(const Ray& ray, const Vec3& origin, const Vec3& normal, Vec3& hit) {
    const float denominator = dot(ray.direction, normal);
    if (!std::isfinite(denominator) || std::abs(denominator) < 0.00001f) { return false; }
    const float t = dot(origin - ray.origin, normal) / denominator;
    if (!std::isfinite(t) || t < 0) { return false; }
    hit = ray.origin + ray.direction * t;
    return finite(hit);
}
bool unit(const Vec3& vector, Vec3& result) {
    const float magnitude = std::hypot(vector.x, vector.y, vector.z);
    if (!std::isfinite(magnitude) || magnitude < 0.000001f) { return false; }
    result = vector * (1.0f / magnitude);
    return true;
}
float component(const Vec3& v, int index) { return index == 0 ? v.x : index == 1 ? v.y : v.z; }
void setComponent(Vec3& v, int index, float value) {
    if (index == 0) { v.x = value; } else if (index == 1) { v.y = value; } else { v.z = value; }
}
Vec3 closestDegrees(Vec3 value, const Vec3& reference) {
    for (int i = 0; i < 3; ++i) {
        const double angle = component(value, i);
        setComponent(value, i, static_cast<float>(angle + 360.0 * std::round((component(reference, i) - angle) / 360.0)));
    }
    return value;
}
double eulerDistance(const Vec3& a, const Vec3& b) {
    const double x = static_cast<double>(a.x) - b.x;
    const double y = static_cast<double>(a.y) - b.y;
    const double z = static_cast<double>(a.z) - b.z;
    return x * x + y * y + z * z;
}
bool cubeHit(float mouseX, float mouseY, const Vec3& center, float extent, const TransformGizmoInstance& gizmo,
    float scale, const Mat4& viewProjection, float width, float height) {
    float minimumX = std::numeric_limits<float>::max(), minimumY = minimumX;
    float maximumX = -minimumX, maximumY = -minimumX;
    bool projected = false;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point = center + Vec3{corner & 1 ? extent : -extent, corner & 2 ? extent : -extent, corner & 4 ? extent : -extent};
        const Vec3 world = gizmo.origin + transformDirection(gizmo.basis, point) * scale;
        float x = 0, y = 0;
        if (project(world, viewProjection, width, height, x, y)) {
            minimumX = std::min(minimumX, x); maximumX = std::max(maximumX, x);
            minimumY = std::min(minimumY, y); maximumY = std::max(maximumY, y);
            projected = true;
        }
    }
    return projected && mouseX >= minimumX - 2 && mouseX <= maximumX + 2 && mouseY >= minimumY - 2 && mouseY <= maximumY + 2;
}
}
Mat4 transformGizmoBasis(TransformTool tool, TransformSpace space, const Vec3& rotationDegrees) {
    return tool == TransformTool::Scale || space == TransformSpace::Local ? rotationMatrixEuler(rotationDegrees) : Mat4{};
}
Vec3 transformGizmoAxis(const Mat4& basis, int axis) {
    assert(axis >= 0 && axis < 3);
    return {basis.m[axis * 4], basis.m[axis * 4 + 1], basis.m[axis * 4 + 2]};
}
Vec3 transformGizmoRingPoint(int axis, float angleRadians) {
    assert(axis >= 0 && axis < 3);
    Vec3 result;
    setComponent(result, (axis + 1) % 3, std::cos(angleRadians) * kTranslateGizmoAxisLength);
    setComponent(result, (axis + 2) % 3, std::sin(angleRadians) * kTranslateGizmoAxisLength);
    return result;
}
Vec3 transformGizmoRingVertex(int axis, int segment) {
    assert(axis >= 0 && axis < 3 && segment >= 0 && segment <= kGizmoRingSegments);
    static const auto kRings = [] {
        std::array<std::array<Vec3, kGizmoRingSegments + 1>, 3> rings{};
        for (int handle = 0; handle < 3; ++handle) {
            for (int vertex = 0; vertex < kGizmoRingSegments; ++vertex) {
                rings[handle][vertex] = transformGizmoRingPoint(handle, static_cast<float>(2 * kPi * vertex / kGizmoRingSegments));
            }
            rings[handle][kGizmoRingSegments] = rings[handle][0];
        }
        return rings;
    }();
    return kRings[axis][segment];
}
GizmoHit pickTransformGizmo(const TransformGizmoInstance& gizmo, const Mat4& view, const Mat4& projection,
    float width, float height, float mouseX, float mouseY) {
    GizmoHit result;
    if (!std::isfinite(width) || width <= 0 || !std::isfinite(mouseX) || !std::isfinite(mouseY)) { return result; }
    const float scale = translateGizmoScale(gizmo.origin, view, projection, height);
    const Mat4 viewProjection = projection * view;
    float x0 = 0, y0 = 0;
    if (scale <= 0 || !project(gizmo.origin, viewProjection, width, height, x0, y0)) { return result; }
    if (gizmo.tool == TransformTool::Scale && cubeHit(mouseX, mouseY, {}, kGizmoCenterHalfExtent, gizmo, scale, viewProjection, width, height)) {
        result.handle = 3; return result;
    }
    float best = kHitPixels;
    for (int axis = 0; axis < 3; ++axis) {
        if (gizmo.tool == TransformTool::Rotate) {
            for (int segment = 0; segment < kGizmoRingSegments; ++segment) {
                const float a = static_cast<float>(2.0 * kPi * segment / kGizmoRingSegments);
                const float b = static_cast<float>(2.0 * kPi * (segment + 1) / kGizmoRingSegments);
                const auto first = gizmo.origin + transformDirection(gizmo.basis, transformGizmoRingVertex(axis, segment)) * scale;
                const auto second = gizmo.origin + transformDirection(gizmo.basis, transformGizmoRingVertex(axis, segment + 1)) * scale;
                float ax = 0, ay = 0, bx = 0, by = 0;
                if (!project(first, viewProjection, width, height, ax, ay) || !project(second, viewProjection, width, height, bx, by)) { continue; }
                float fraction = 0;
                const float distance = segmentDistance(mouseX, mouseY, ax, ay, bx, by, fraction);
                if (distance < best) { best = distance; result = {axis, a + (b - a) * fraction}; }
            }
        } else {
            const auto axisVector = transformGizmoAxis(gizmo.basis, axis);
            const auto tip = gizmo.origin + axisVector * (kTranslateGizmoAxisLength * scale);
            float x1 = 0, y1 = 0;
            if (!project(tip, viewProjection, width, height, x1, y1) || std::hypot(x1 - x0, y1 - y0) < 2) { continue; }
            float fraction = 0;
            float distance = segmentDistance(mouseX, mouseY, x0, y0, x1, y1, fraction);
            if (gizmo.tool == TransformTool::Scale && cubeHit(mouseX, mouseY,
                transformGizmoAxis(Mat4{}, axis) * kTranslateGizmoAxisLength, kGizmoScaleTipHalfExtent,
                gizmo, scale, viewProjection, width, height)) { distance = 0; }
            if (distance < best) { best = distance; result.handle = axis; }
        }
    }
    return result;
}
bool sameTransform(const Transform& a, const Transform& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
        a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
        a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z;
}
Mat4 gizmoAxisRotation(int axis, double radians) {
    assert(axis >= 0 && axis < 3);
    const float cosine = static_cast<float>(std::cos(std::remainder(radians, 2 * kPi)));
    const float sine = static_cast<float>(std::sin(std::remainder(radians, 2 * kPi)));
    Mat4 result;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    result.m[u * 4 + u] = cosine; result.m[v * 4 + v] = cosine;
    result.m[u * 4 + v] = sine; result.m[v * 4 + u] = -sine;
    return result;
}
Vec3 gizmoEulerDegrees(const Mat4& rotation, const Vec3& referenceDegrees) {
    const double cy = std::hypot(static_cast<double>(rotation.m[0]), static_cast<double>(rotation.m[1]));
    const double yaw = std::atan2(-static_cast<double>(rotation.m[2]), cy);
    constexpr double kRadiansToDegrees = 180.0 / kPi;
    if (cy < 0.000001) {
        const double roll = referenceDegrees.z / kRadiansToDegrees;
        const double pitch = yaw > 0 ? std::atan2(rotation.m[4], rotation.m[5]) + roll :
            std::atan2(-rotation.m[4], rotation.m[5]) - roll;
        return closestDegrees({static_cast<float>(pitch * kRadiansToDegrees), yaw > 0 ? 90.0f : -90.0f, referenceDegrees.z}, referenceDegrees);
    }
    const Vec3 first{static_cast<float>(std::atan2(rotation.m[6], rotation.m[10]) * kRadiansToDegrees),
        static_cast<float>(yaw * kRadiansToDegrees), static_cast<float>(std::atan2(rotation.m[1], rotation.m[0]) * kRadiansToDegrees)};
    const auto a = closestDegrees(first, referenceDegrees);
    const auto b = closestDegrees({first.x + 180, 180 - first.y, first.z + 180}, referenceDegrees);
    return eulerDistance(a, referenceDegrees) <= eulerDistance(b, referenceDegrees) ? a : b;
}
bool TransformGizmoDrag::begin(EntityId owner, const Transform& transform, TransformTool tool, TransformSpace space,
    GizmoHit hit, const Mat4& view, const Mat4& projection, float width, float height, float mouseX, float mouseY) {
    if (m_active) { return false; }
    if (owner == kInvalidEntityId || !finite(transform) || hit.handle < 0 || hit.handle > (tool == TransformTool::Scale ? 3 : 2) ||
        !std::isfinite(width) || width <= 0 || !std::isfinite(mouseX) || !std::isfinite(mouseY)) { return false; }
    m_origin = toVec3(transform.position);
    const float scale = translateGizmoScale(m_origin, view, projection, height);
    if (scale <= 0) { return false; }
    const Mat4 viewProjection = projection * view;
    m_tool = tool; m_space = space; m_owner = owner; m_handle = hit.handle;
    m_start = transform; m_last = transform; m_rotation = rotationMatrixEuler(transform.rotation);
    m_view = view; m_projection = projection; m_width = width; m_height = height;
    m_startX = m_lastX = mouseX; m_startY = m_lastY = mouseY; m_angle = 0;
    m_worldLength = scale * kTranslateGizmoAxisLength;
    m_usePlane = false;
    if (hit.handle == 3) {
        m_screenX = 0.70710678f; m_screenY = -0.70710678f; m_screenLength = kTranslateGizmoAxisPixels;
        m_active = true; return true;
    }
    const auto basis = transformGizmoBasis(tool, space, transform.rotation);
    m_axis = transformGizmoAxis(basis, hit.handle);
    const Ray ray = unprojectRay(mouseX, mouseY, width, height, view, projection);
    if (tool == TransformTool::Rotate) {
        m_planeNormal = m_axis;
        Vec3 point;
        m_usePlane = std::abs(dot(ray.direction, m_axis)) > 0.05f && planeHit(ray, m_origin, m_axis, point) && unit(point - m_origin, m_radial);
        if (!m_usePlane) {
            const auto radial = transformDirection(basis, transformGizmoRingPoint(hit.handle, hit.ringAngle));
            const auto next = transformDirection(basis, transformGizmoRingPoint(hit.handle, hit.ringAngle + 0.01f));
            float ax = 0, ay = 0, bx = 0, by = 0;
            if (!project(m_origin + radial * scale, viewProjection, width, height, ax, ay) ||
                !project(m_origin + next * scale, viewProjection, width, height, bx, by)) { return false; }
            const float length = std::hypot(bx - ax, by - ay);
            if (length < 0.01f) { return false; }
            m_screenX = (bx - ax) / length; m_screenY = (by - ay) / length;
            m_screenLength = length / 0.01f; // Pixels per radian at the clicked ring tangent.
        }
    } else {
        float ax = 0, ay = 0, bx = 0, by = 0;
        if (!project(m_origin, viewProjection, width, height, ax, ay) ||
            !project(m_origin + m_axis * static_cast<float>(m_worldLength), viewProjection, width, height, bx, by)) { return false; }
        m_screenLength = std::hypot(bx - ax, by - ay);
        if (m_screenLength < 2) { return false; }
        m_screenX = (bx - ax) / m_screenLength; m_screenY = (by - ay) / m_screenLength;
        if (tool == TransformTool::Move) {
            const Vec3 forward{view.m[2], view.m[6], view.m[10]};
            Vec3 point;
            m_usePlane = unit(cross(m_axis, cross(forward, m_axis)), m_planeNormal) &&
                std::abs(dot(ray.direction, m_planeNormal)) > 0.05f && planeHit(ray, m_origin, m_planeNormal, point);
            if (m_usePlane) { m_startOffset = dot(point - m_origin, m_axis); }
        }
    }
    m_active = true;
    return true;
}
bool TransformGizmoDrag::update(Scene& scene, float mouseX, float mouseY) {
    if (!m_active) { return false; }
    Entity* entity = scene.entityById(m_owner);
    if (!entity) { m_active = false; return false; }
    if (!std::isfinite(mouseX) || !std::isfinite(mouseY) || (mouseX == m_lastX && mouseY == m_lastY)) { return false; }
    Transform next = m_start;
    const double pixels = (static_cast<double>(mouseX) - m_startX) * m_screenX + (static_cast<double>(mouseY) - m_startY) * m_screenY;
    const Ray ray = unprojectRay(mouseX, mouseY, m_width, m_height, m_view, m_projection);
    if (m_tool == TransformTool::Move) {
        double delta = pixels * m_worldLength / m_screenLength;
        if (m_usePlane) {
            Vec3 hit;
            if (!planeHit(ray, m_origin, m_planeNormal, hit)) { return false; }
            delta = static_cast<double>(dot(hit - m_origin, m_axis)) - m_startOffset;
        }
        next.position = m_start.position + DVec3{m_axis.x * delta, m_axis.y * delta, m_axis.z * delta};
    } else if (m_tool == TransformTool::Rotate) {
        if (m_usePlane) {
            Vec3 hit, radial;
            if (!planeHit(ray, m_origin, m_axis, hit) || !unit(hit - m_origin, radial)) { return false; }
            m_angle += std::atan2(static_cast<double>(dot(m_axis, cross(m_radial, radial))),
                static_cast<double>(std::clamp(dot(m_radial, radial), -1.0f, 1.0f)));
            m_radial = radial;
        } else {
            m_angle += ((static_cast<double>(mouseX) - m_lastX) * m_screenX + (static_cast<double>(mouseY) - m_lastY) * m_screenY) / m_screenLength;
        }
        const auto delta = gizmoAxisRotation(m_handle, m_angle);
        next.rotation = gizmoEulerDegrees(m_space == TransformSpace::Local ? m_rotation * delta : delta * m_rotation, m_last.rotation);
    } else {
        if (mouseX != m_startX || mouseY != m_startY) {
            double factor = 1.0 + pixels / m_screenLength;
            double minimum = 0, maximum = std::numeric_limits<double>::max();
            for (int axis = 0; axis < 3; ++axis) {
                if (m_handle != 3 && axis != m_handle) { continue; }
                const double magnitude = std::max(static_cast<double>(std::abs(component(m_start.scale, axis))), static_cast<double>(kGizmoMinimumScale));
                minimum = std::max(minimum, kGizmoMinimumScale / magnitude);
                maximum = std::min(maximum, std::numeric_limits<float>::max() / magnitude);
            }
            factor = std::clamp(factor, minimum, maximum);
            for (int axis = 0; axis < 3; ++axis) {
                if (m_handle != 3 && axis != m_handle) { continue; }
                const float source = component(m_start.scale, axis);
                const double magnitude = std::max(static_cast<double>(std::abs(source)), static_cast<double>(kGizmoMinimumScale));
                setComponent(next.scale, axis, static_cast<float>((source < 0 ? -1 : 1) * magnitude * factor));
            }
        }
    }
    m_lastX = mouseX; m_lastY = mouseY;
    if (!finite(next) || sameTransform(next, m_last)) { return false; }
    m_last = next;
    if (sameTransform(entity->transform, next)) { return false; }
    entity->transform = next;
    scene.markEdited();
    return true;
}
bool TransformGizmoDrag::cancel(Scene& scene) {
    if (!m_active) { return false; }
    m_active = false;
    auto* entity = scene.entityById(m_owner);
    if (!entity || sameTransform(entity->transform, m_start)) { return false; }
    entity->transform = m_start;
    scene.markEdited();
    return true;
}
}
