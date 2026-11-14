#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>

namespace sat {

// Non-owning view of GPU mesh buffers used by mesh / outline passes.
struct GpuMesh {
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* indexBuffer = nullptr;
    UINT indexCount = 0;
    UINT vertexStride = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_R16_UINT;
    float boundsCenter[3]{}; // Mesh-local bounding sphere.
    float boundsRadius = 0.0f;

    [[nodiscard]] bool valid() const {
        return vertexBuffer != nullptr && indexBuffer != nullptr && indexCount > 0 &&
            vertexStride > 0;
    }
};

} // namespace sat
