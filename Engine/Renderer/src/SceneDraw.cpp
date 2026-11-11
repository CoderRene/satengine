#include "SceneDraw.hpp"

#include <cmath>
#include <limits>

namespace sat {
float translateGizmoScale(const Vec3& origin, const Mat4& view, const Mat4& projection, float viewportHeight) {
    if (!std::isfinite(viewportHeight) || viewportHeight <= 0.0f ||
        !std::isfinite(projection.m[5]) || projection.m[5] <= 0.0f) {
        return 0.0f;
    }
    // Use camera depth, not Euclidean distance: moving off-center must not
    // enlarge the handles. Double intermediates avoid overflow during validation.
    const double x = static_cast<double>(view.m[0]) * origin.x + static_cast<double>(view.m[4]) * origin.y +
        static_cast<double>(view.m[8]) * origin.z + view.m[12];
    const double y = static_cast<double>(view.m[1]) * origin.x + static_cast<double>(view.m[5]) * origin.y +
        static_cast<double>(view.m[9]) * origin.z + view.m[13];
    const double z = static_cast<double>(view.m[2]) * origin.x + static_cast<double>(view.m[6]) * origin.y +
        static_cast<double>(view.m[10]) * origin.z + view.m[14];
    const double w = static_cast<double>(view.m[3]) * origin.x + static_cast<double>(view.m[7]) * origin.y +
        static_cast<double>(view.m[11]) * origin.z + view.m[15];
    const double clipW = projection.m[3] * x + projection.m[7] * y + projection.m[11] * z + projection.m[15] * w;
    const double clipZ = projection.m[2] * x + projection.m[6] * y + projection.m[10] * z + projection.m[14] * w;
    if (!std::isfinite(clipW) || !std::isfinite(clipZ) || clipW <= 0.0001 || clipZ < 0.0 || clipZ > clipW) {
        return 0.0f;
    }
    // NDC spans two units vertically. Orthographic projection has constant W,
    // so the same formula also keeps its handles independent of camera depth.
    const double scale = (2.0 * kTranslateGizmoAxisPixels * clipW) /
        (static_cast<double>(projection.m[5]) * viewportHeight * kTranslateGizmoAxisLength);
    if (!std::isfinite(scale) || scale <= 0.0 || scale > std::numeric_limits<float>::max()) { return 0.0f; }
    return static_cast<float>(scale);
}
}
