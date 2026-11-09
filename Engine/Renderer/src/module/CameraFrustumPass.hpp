#pragma once

#include "SceneDraw.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace sat {

class CameraFrustumPass {
public:
    CameraFrustumPass() = default;
    ~CameraFrustumPass();

    CameraFrustumPass(const CameraFrustumPass&) = delete;
    CameraFrustumPass& operator=(const CameraFrustumPass&) = delete;

    [[nodiscard]] bool ensure(ID3D11Device* device);
    void release();
    void draw(
        ID3D11DeviceContext* context,
        const Mat4& viewProj,
        const CameraFrustumInstance* frustums,
        int frustumCount);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace sat
