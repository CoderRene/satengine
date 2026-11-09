#pragma once

#include "Math.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class TranslateGizmoPass {
public:
    TranslateGizmoPass() = default;
    ~TranslateGizmoPass();

    TranslateGizmoPass(const TranslateGizmoPass&) = delete;
    TranslateGizmoPass& operator=(const TranslateGizmoPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context,
        const Mat4& viewProj,
        const Vec3& origin) const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace sat
