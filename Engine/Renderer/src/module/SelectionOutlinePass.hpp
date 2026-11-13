#pragma once

#include "ShaderLibrary.hpp"

#include "GpuMesh.hpp"
#include "SceneDraw.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class SelectionOutlinePass {
public:
    SelectionOutlinePass() = default;
    ~SelectionOutlinePass();

    SelectionOutlinePass(const SelectionOutlinePass&) = delete;
    SelectionOutlinePass& operator=(const SelectionOutlinePass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context, ShaderProgram program,
        const Mat4& viewProj,
        const MeshInstance& selected,
        const GpuMesh& geometry) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace sat
