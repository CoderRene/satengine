#pragma once

#include <cmath>
#include <cstring>

namespace sat {

inline constexpr float kDegToRad = 0.01745329251f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Absolute/simulation world positions. Narrow to Vec3 only at the render boundary.
struct DVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    static Mat4 identity() {
        return Mat4{};
    }
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline Vec3 operator*(const Vec3& v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}

inline DVec3 operator+(const DVec3& a, const DVec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

inline DVec3 operator-(const DVec3& a, const DVec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline DVec3 operator*(const DVec3& v, double s) {
    return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]] inline Vec3 toVec3(const DVec3& v) {
    return {
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z),
    };
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    if (len <= 0.000001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return v * (1.0f / len);
}

// Column-major left-handed perspective (D3D clip Z 0..1).
inline Mat4 perspective(float fovYDegrees, float aspect, float nearPlane, float farPlane) {
    Mat4 out{};
    if (aspect < 0.0001f || nearPlane <= 0.0f || farPlane <= nearPlane) {
        return out;
    }

    const float yScale = 1.0f / std::tan(fovYDegrees * kDegToRad * 0.5f);
    const float xScale = yScale / aspect;
    const float q = farPlane / (farPlane - nearPlane);

    std::memset(out.m, 0, sizeof(out.m));
    out.m[0] = xScale;
    out.m[5] = yScale;
    out.m[10] = q;
    out.m[11] = 1.0f;
    out.m[14] = -nearPlane * q;
    return out;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 zAxis = normalize(target - eye);
    const Vec3 xAxis = normalize(cross(up, zAxis));
    const Vec3 yAxis = cross(zAxis, xAxis);

    Mat4 out{};
    out.m[0] = xAxis.x;
    out.m[1] = yAxis.x;
    out.m[2] = zAxis.x;
    out.m[3] = 0.0f;
    out.m[4] = xAxis.y;
    out.m[5] = yAxis.y;
    out.m[6] = zAxis.y;
    out.m[7] = 0.0f;
    out.m[8] = xAxis.z;
    out.m[9] = yAxis.z;
    out.m[10] = zAxis.z;
    out.m[11] = 0.0f;
    out.m[12] = -dot(xAxis, eye);
    out.m[13] = -dot(yAxis, eye);
    out.m[14] = -dot(zAxis, eye);
    out.m[15] = 1.0f;
    return out;
}

// Euler degrees: x=pitch, y=yaw, z=roll. Scale is ignored for the view.
inline Mat4 viewFromEuler(const Vec3& position, const Vec3& rotationDegrees) {
    const float pitch = rotationDegrees.x * kDegToRad;
    const float yaw = rotationDegrees.y * kDegToRad;

    const float cosPitch = std::cos(pitch);
    const Vec3 forward = normalize(Vec3{
        cosPitch * std::sin(yaw),
        std::sin(pitch),
        cosPitch * std::cos(yaw),
    });

    return lookAt(position, position + forward, Vec3{0.0f, 1.0f, 0.0f});
}

inline Mat4 translationMatrix(const Vec3& t) {
    Mat4 out = Mat4::identity();
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

inline Mat4 scalingMatrix(const Vec3& s) {
    Mat4 out{};
    std::memset(out.m, 0, sizeof(out.m));
    out.m[0] = s.x;
    out.m[5] = s.y;
    out.m[10] = s.z;
    out.m[15] = 1.0f;
    return out;
}

// Euler degrees: x=pitch, y=yaw, z=roll. Column-major, applied as Rz * Ry * Rx.
inline Mat4 rotationMatrixEuler(const Vec3& rotationDegrees) {
    const float pitch = rotationDegrees.x * kDegToRad;
    const float yaw = rotationDegrees.y * kDegToRad;
    const float roll = rotationDegrees.z * kDegToRad;

    const float cx = std::cos(pitch);
    const float sx = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cz = std::cos(roll);
    const float sz = std::sin(roll);

    Mat4 out{};
    out.m[0] = cy * cz;
    out.m[1] = cy * sz;
    out.m[2] = -sy;
    out.m[3] = 0.0f;

    out.m[4] = sx * sy * cz - cx * sz;
    out.m[5] = sx * sy * sz + cx * cz;
    out.m[6] = sx * cy;
    out.m[7] = 0.0f;

    out.m[8] = cx * sy * cz + sx * sz;
    out.m[9] = cx * sy * sz - sx * cz;
    out.m[10] = cx * cy;
    out.m[11] = 0.0f;

    out.m[12] = 0.0f;
    out.m[13] = 0.0f;
    out.m[14] = 0.0f;
    out.m[15] = 1.0f;
    return out;
}

inline Mat4 matrixFromTRS(
    const Vec3& position, const Vec3& rotationDegrees, const Vec3& scale) {
    return translationMatrix(position) * rotationMatrixEuler(rotationDegrees) *
        scalingMatrix(scale);
}

inline Mat4 inverse(const Mat4& m) {
    const float* a = m.m;
    Mat4 out{};
    float* o = out.m;

    o[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] +
        a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    o[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] -
        a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    o[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] +
        a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    o[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] -
        a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    o[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] -
        a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    o[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] +
        a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    o[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] -
        a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    o[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] +
        a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    o[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] +
        a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    o[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] -
        a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    o[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] +
        a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    o[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] -
        a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    o[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] -
        a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    o[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] +
        a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    o[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] -
        a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    o[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] +
        a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    const float det = a[0] * o[0] + a[1] * o[4] + a[2] * o[8] + a[3] * o[12];
    if (std::fabs(det) < 1.0e-8f) {
        return Mat4::identity();
    }
    const float invDet = 1.0f / det;
    for (float& v : out.m) {
        v *= invDet;
    }
    return out;
}

struct Ray {
    Vec3 origin{};
    Vec3 direction{0.0f, 0.0f, 1.0f};
};

inline Vec3 transformPoint(const Mat4& m, const Vec3& p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
    };
}

inline Vec3 transformPointH(const Mat4& m, float x, float y, float z, float w = 1.0f) {
    const float ox = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12] * w;
    const float oy = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13] * w;
    const float oz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14] * w;
    const float ow = m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w;
    if (std::fabs(ow) < 1.0e-8f) {
        return {ox, oy, oz};
    }
    const float invW = 1.0f / ow;
    return {ox * invW, oy * invW, oz * invW};
}

inline Vec3 transformDirection(const Mat4& m, const Vec3& d) {
    return {
        m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z,
        m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
        m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z,
    };
}

// Pixel coords: origin top-left of the view, y down (ImGui style).
inline Ray unprojectRay(
    float pixelX,
    float pixelY,
    float viewWidth,
    float viewHeight,
    const Mat4& view,
    const Mat4& projection) {
    Ray ray{};
    if (viewWidth < 1.0f || viewHeight < 1.0f) {
        return ray;
    }

    const float ndcX = (pixelX / viewWidth) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (pixelY / viewHeight) * 2.0f;

    const Mat4 invViewProj = inverse(projection * view);
    const Vec3 nearPoint = transformPointH(invViewProj, ndcX, ndcY, 0.0f, 1.0f);
    const Vec3 farPoint = transformPointH(invViewProj, ndcX, ndcY, 1.0f, 1.0f);
    ray.origin = nearPoint;
    ray.direction = normalize(farPoint - nearPoint);
    return ray;
}

// Returns false if the point is behind the camera.
inline bool projectWorldToScreen(
    const Vec3& world,
    const Mat4& view,
    const Mat4& projection,
    float viewWidth,
    float viewHeight,
    float& outX,
    float& outY) {
    const Mat4 viewProj = projection * view;
    const float x = viewProj.m[0] * world.x + viewProj.m[4] * world.y + viewProj.m[8] * world.z +
        viewProj.m[12];
    const float y = viewProj.m[1] * world.x + viewProj.m[5] * world.y + viewProj.m[9] * world.z +
        viewProj.m[13];
    const float w = viewProj.m[3] * world.x + viewProj.m[7] * world.y + viewProj.m[11] * world.z +
        viewProj.m[15];
    if (w <= 0.0001f) {
        return false;
    }
    const float ndcX = x / w;
    const float ndcY = y / w;
    outX = (ndcX * 0.5f + 0.5f) * viewWidth;
    outY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewHeight;
    return true;
}

} // namespace sat
