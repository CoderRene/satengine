#pragma once

#include "AssetData.hpp"
#include "Math.hpp"

#include "ShaderLibrary.hpp"

#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace sat {

struct alignas(16) MeshFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float eye[3];
    unsigned lightCount;
    float ambient[4];
    float viewport[4];
    float environmentIntensity;
    int shadowLight;
    float lightingPadding[2];
    float cascadeViewProj[4][16];
    float cascadeSplits[4];
    float cascadeTexel[4];
    unsigned cascadeCount;
    float cascadeBlend;
    float cascadeNear;
    float orthoDepthScale;
};

struct alignas(16) MeshInstanceData {
    float world[16];
    float normal[12]; // Three columns. The first column's W stores tangent handedness.
};

inline void writeMeshInstance(MeshInstanceData& object, const Mat4& matrix) {
    std::memcpy(object.world, matrix.m, sizeof(object.world));
    // Orthogonal TRS columns give inverse-transpose via reciprocal squared lengths.
    const auto& world = matrix.m;
    const Vec3 x{world[0], world[1], world[2]};
    const Vec3 y{world[4], world[5], world[6]};
    const Vec3 z{world[8], world[9], world[10]};
    const float xx = dot(x, x), yy = dot(y, y), zz = dot(z, z);
    const float tolerance = 1e-5f * std::max({xx, yy, zz});
    const bool uniform = std::abs(xx - yy) <= tolerance && std::abs(xx - zz) <= tolerance;
    const float sign = dot(cross(x, y), z) < 0 ? -1.0f : 1.0f;
    const float scales[] = {xx, yy, zz};
    for (int column = 0; column < 3; ++column) {
        const float scale = uniform || scales[column] <= 1e-20f ? 1.0f : 1.0f / scales[column];
        for (int row = 0; row < 3; ++row) {
            object.normal[column * 4 + row] = world[column * 4 + row] * scale;
        }
        object.normal[column * 4 + 3] = 0;
    }
    object.normal[3] = sign;
}

struct alignas(16) MaterialCb {
    float baseColor[4];
    float emissive[3];
    float normalScale;
    float metallic;
    float roughness;
    float hasEmissiveTexture;
    float unlit;
    float alphaMode, alphaCutoff, specularStrength, occlusionStrength;
};

inline constexpr int kMaxLights = 128;
struct alignas(16) PackedLight {
    float position[3], type;
    float direction[3], range;
    float radiance[3], innerCos;
    float outerCos, padding[3];
};
struct alignas(16) LightsCb {
    PackedLight lights[kMaxLights];
};

inline constexpr int kClusterTiles = 8;
inline constexpr int kClusterSlices = 8;
struct alignas(16) ClusterCb {
    unsigned masks[kClusterSlices][kClusterTiles][kClusterTiles][4];
};
static_assert(sizeof(ClusterCb) == 8192 && alignof(ClusterCb) == 16);

struct alignas(16) BloomCb {
    float threshold, knee, intensity, radius;
    float tint[3], highlightClamp;
    unsigned kernelRadius;
    unsigned pass;
    float coarseWeight;
    unsigned padding;
};
static_assert(sizeof(BloomCb) == 48 && alignof(BloomCb) == 16);

struct alignas(16) GridFrameCb {
    float viewProj[16];
};

struct alignas(16) OutlineFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

struct alignas(16) OutlineObjectCb {
    float world[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct alignas(16) FrustumFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
};

struct alignas(16) GizmoFrameCb {
    float viewProj[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float selection[4] = {-1, 0, 0, 0};
};

static_assert(sizeof(MeshVertex) == 48 && offsetof(MeshVertex, nx) == 12 &&
    offsetof(MeshVertex, u) == 24 && offsetof(MeshVertex, tx) == 32);
static_assert(sizeof(MeshFrameCb) == 432 && alignof(MeshFrameCb) == 16 &&
    offsetof(MeshFrameCb, cascadeViewProj) == 128 && offsetof(MeshFrameCb, cascadeSplits) == 384 &&
    offsetof(MeshFrameCb, cascadeTexel) == 400 && offsetof(MeshFrameCb, cascadeCount) == 416);
static_assert(sizeof(MeshInstanceData) == 112 && offsetof(MeshInstanceData, normal) == 64);
static_assert(sizeof(MaterialCb) == 64 && alignof(MaterialCb) == 16);
static_assert(sizeof(PackedLight) == 64 && alignof(PackedLight) == 16);
static_assert(sizeof(LightsCb) == 8192 && alignof(LightsCb) == 16);
static_assert(sizeof(OutlineFrameCb) == 64 && alignof(OutlineFrameCb) == 16);
static_assert(sizeof(OutlineObjectCb) == 80 && alignof(OutlineObjectCb) == 16);
static_assert(sizeof(FrustumFrameCb) == 64 && alignof(FrustumFrameCb) == 16);
static_assert(sizeof(GizmoFrameCb) == 80 && alignof(GizmoFrameCb) == 16);
static_assert(sizeof(GridFrameCb) == 64 && alignof(GridFrameCb) == 16);

inline constexpr D3D11_INPUT_ELEMENT_DESC kMeshLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 5, DXGI_FORMAT_R32G32B32_FLOAT, 1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE", 6, DXGI_FORMAT_R32G32B32_FLOAT, 1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

inline constexpr D3D11_INPUT_ELEMENT_DESC kSelectionOutlineLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

inline constexpr D3D11_INPUT_ELEMENT_DESC kCameraFrustumLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };

inline constexpr D3D11_INPUT_ELEMENT_DESC kTransformGizmoLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA,
         0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

struct alignas(16) UiDrawCb {
    float transform[16];
    float translation[2];
    float scale[2];
};
static_assert(sizeof(UiDrawCb) == 80);

struct alignas(16) UiEffectCb {
    float matrix[16];
    float color[4];
    float offset[2];
    float step[2];
    float value;
    unsigned effect;
    float padding[2];
};

static_assert(sizeof(UiEffectCb) == 112);

inline constexpr unsigned kUiGradientStops = 64;
struct alignas(16) UiGradientCb {
    float origin[2];
    float direction[2];
    unsigned gradientType;
    unsigned repeating;
    unsigned stopCount;
    unsigned padding;
    float positions[kUiGradientStops];
    float colors[kUiGradientStops][4];
};

static_assert(sizeof(UiGradientCb) == 1312);

inline constexpr D3D11_INPUT_ELEMENT_DESC kUiLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Rml::Vertex, colour), D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Rml::Vertex, tex_coord), D3D11_INPUT_PER_VERTEX_DATA, 0},
};

inline constexpr D3D11_INPUT_ELEMENT_DESC kGridLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",
         0,
         DXGI_FORMAT_R32G32B32A32_FLOAT,
         0,
         D3D11_APPEND_ALIGNED_ELEMENT,
         D3D11_INPUT_PER_VERTEX_DATA,
         0},
    };

} // namespace sat
