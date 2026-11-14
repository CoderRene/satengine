#pragma once

#include "Math.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <wrl/client.h>

#include <array>

namespace sat {

struct SceneDrawList;

// Used on the immediate render context. Matrices and caster centers share the
// view's rebased world coordinates, while the texel grid is anchored in absolute world space.
class CascadedShadowMap {
public:
    static constexpr unsigned kMaxCascades = 4;
    static constexpr float kBlendFraction = 0.1f;

    struct Cascade {
        Mat4 viewProj;
        Vec3 center;
        float halfSize = 0;
        float farOffset = 0;
        float split = 0;
        float texelSize = 0;
    };

    CascadedShadowMap() = default;
    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

    [[nodiscard]] bool update(ID3D11DeviceContext* context, const Mat4& view, const Mat4& projection,
        DVec3 renderOrigin, Vec3 lightDirection, const SceneDrawList& settings);
    [[nodiscard]] Vec3 lightSpaceCenter(Vec3 center) const;
    [[nodiscard]] bool overlaps(unsigned index, Vec3 lightCenter, float radius) const;

    [[nodiscard]] const Cascade& cascade(unsigned index) const { return m_cascades[index]; }
    [[nodiscard]] float nearDepth() const { return m_nearDepth; }
    [[nodiscard]] unsigned count() const { return m_countAttempted; }
    [[nodiscard]] unsigned resolution() const { return m_resolutionAttempted; }
    [[nodiscard]] ID3D11DepthStencilView* depthView(unsigned index) const { return m_depthViews[index].Get(); }
    [[nodiscard]] ID3D11ShaderResourceView* shaderView() const { return m_shaderView.Get(); }
    [[nodiscard]] ID3D11SamplerState* sampler() const { return m_sampler.Get(); }
    [[nodiscard]] ID3D11RasterizerState* rasterizer() const { return m_rasterizer.Get(); }

private:
    [[nodiscard]] bool ensureResources(ID3D11DeviceContext* context, unsigned resolution, unsigned count);

    std::array<Cascade, kMaxCascades> m_cascades;
    Vec3 m_lightX;
    Vec3 m_lightY;
    Vec3 m_lightZ;
    float m_nearDepth = 0;
    std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>, kMaxCascades> m_depthViews;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shaderView;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    unsigned m_resolutionAttempted = 0;
    unsigned m_countAttempted = 0;
};

} // namespace sat
