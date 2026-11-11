#pragma once

#include "SceneDraw.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class TransformGizmoPass {
public:
    TransformGizmoPass() = default;
    ~TransformGizmoPass();

    TransformGizmoPass(const TransformGizmoPass&) = delete;
    TransformGizmoPass& operator=(const TransformGizmoPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context,
        const Mat4& viewProj,
        const TransformGizmoInstance& gizmo,
        float worldScale) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace sat
