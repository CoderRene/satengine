#pragma once

#include "AssetData.hpp"
#include "Math.hpp"

#include "ShaderLibrary.hpp"

#include <RmlUi/Core/Vertex.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstddef>

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
    float environmentMaxLod;
    float lightingPadding;
    float cascadeViewProj[4][16];
    float cascadeSplits[4];
    float cascadeTexel[4];
    unsigned cascadeCount;
    float cascadeBlend;
    float cascadeNear;
    float orthoDepthScale;
    float orthoViewToEye[3];
    unsigned orthographic;
    float originHigh[4];
    float originLow[4];
    float spotViewProj[16];
    int spotLight;
    float spotPadding[3];
};

inline void writeNormalColumns(float* normal, const Mat4& matrix) {
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
            normal[column * 4 + row] = world[column * 4 + row] * scale;
        }
        normal[column * 4 + 3] = 0;
    }
    normal[3] = sign;
}

struct alignas(16) GpuInstance {
    float basis[3][4];
    float normal[3][4];
    float translationHigh[3];
    unsigned paletteOffset; // Skinned vertex base for skinned instances.
    float translationLow[3];
    unsigned flags;
    float bounds[4]; // Center relative to split translation, then radius.
    float thicknessScale[4];
};
static_assert(sizeof(GpuInstance) == 160 && offsetof(GpuInstance, normal) == 48 &&
    offsetof(GpuInstance, translationHigh) == 96 && offsetof(GpuInstance, translationLow) == 112 &&
    offsetof(GpuInstance, bounds) == 128 && offsetof(GpuInstance, thicknessScale) == 144);

struct alignas(16) DirtyGpuInstance {
    unsigned index;
    unsigned padding[3];
    GpuInstance data;
};
static_assert(sizeof(DirtyGpuInstance) == 176 && offsetof(DirtyGpuInstance, data) == 16);

struct GpuDrawItem {
    unsigned instance, batch, visibleStart, passes;
};
struct GpuBatch {
    unsigned indexCount, instanceCount, firstIndex;
    int baseVertex;
    unsigned firstInstance;
};
static_assert(sizeof(GpuDrawItem) == 16 && sizeof(GpuBatch) == 20 && offsetof(GpuBatch, instanceCount) == 4);

struct alignas(16) SkinCb { unsigned vertexBase = 0; unsigned padding[3]{}; };
struct alignas(16) SkinDispatchCb { unsigned vertexStart, vertexCount, paletteOffset, outputBase; };
struct SkinnedVertex { float position[3], normal[3], tangent[4]; };
static_assert(sizeof(SkinDispatchCb) == 16 && sizeof(SkinnedVertex) == 40);
// Raw buffer layout consumed by SkinVertices.slang.
static_assert(sizeof(MeshVertex) == 72 && offsetof(MeshVertex, x) == 0 &&
    offsetof(MeshVertex, nx) == 12 && offsetof(MeshVertex, tx) == 32);
static_assert(offsetof(MeshSkinVertex, joints) == 0 && offsetof(MeshSkinVertex, weights) == 4);

struct MaterialUvCb {
    float matrix[4]; // UV transform rows.
    float offset[2];
    unsigned texCoord;
    unsigned useDerivativeFrame;
};
static_assert(sizeof(MaterialUvCb) == 32);

struct alignas(16) MaterialCb {
    float baseColor[4];
    float emissive[3];
    float normalScale;
    float metallic;
    float roughness;
    float hasEmissiveTexture;
    float unlit;
    float alphaMode, alphaCutoff, specularStrength, occlusionStrength;
    MaterialUvCb uv[7];
    float transmission, ior, thickness, attenuationDistance;
    float attenuationColor[3];
    float transmissionMip;
    MaterialUvCb coatUv[3];
    float clearcoat, clearcoatRoughness, clearcoatNormalScale, hasClearcoatNormal;
    float transmissionRoughnessStrength;
    float transmissionPadding[3];
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

static_assert(sizeof(MeshVertex) == 72 && offsetof(MeshVertex, nx) == 12 &&
    offsetof(MeshVertex, u) == 24 && offsetof(MeshVertex, tx) == 32 &&
    offsetof(MeshVertex, u1) == 48 && offsetof(MeshVertex, color) == 56);
static_assert(sizeof(MeshFrameCb) == 560 && alignof(MeshFrameCb) == 16 &&
    offsetof(MeshFrameCb, cascadeViewProj) == 128 && offsetof(MeshFrameCb, cascadeSplits) == 384 &&
    offsetof(MeshFrameCb, cascadeTexel) == 400 && offsetof(MeshFrameCb, cascadeCount) == 416 &&
    offsetof(MeshFrameCb, originHigh) == 448 && offsetof(MeshFrameCb, originLow) == 464 &&
    offsetof(MeshFrameCb, spotViewProj) == 480 && offsetof(MeshFrameCb, spotLight) == 544);
static_assert(sizeof(MaterialCb) == 448 && offsetof(MaterialCb, uv) == 64 &&
    offsetof(MaterialCb, transmission) == 288 && offsetof(MaterialCb, coatUv) == 320 &&
    offsetof(MaterialCb, clearcoat) == 416 && offsetof(MaterialCb, transmissionRoughnessStrength) == 432 &&
    alignof(MaterialCb) == 16);
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
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(MeshVertex, u1), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(MeshVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"INSTANCE", 0, DXGI_FORMAT_R32_UINT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

inline constexpr D3D11_INPUT_ELEMENT_DESC kMeshSkinnedLayout[] = {
    kMeshLayout[2], kMeshLayout[4], kMeshLayout[5], kMeshLayout[6],
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
