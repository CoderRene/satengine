#pragma once

#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class MeshPass {
public:
    MeshPass() = default;
    ~MeshPass();

    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context,
        const Mat4& viewProj,
        const SceneDrawList& drawList) const;

    // Geometry owned by this pass for the given primitive (nullptr if unknown/unready).
    [[nodiscard]] const GpuMesh* geometry(MeshPrimitive primitive) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace sat
