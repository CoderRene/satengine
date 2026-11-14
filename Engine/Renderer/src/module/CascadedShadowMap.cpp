#include "CascadedShadowMap.hpp"

#include "SceneDraw.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>

namespace sat {
namespace {

double lightCoordinate(Vec3 axis, DVec3 origin) {
    return axis.x * origin.x + axis.y * origin.y + axis.z * origin.z;
}

float snapToTexel(float center, double absoluteOrigin, float texelSize) {
    // Keep the grid phase in double precision and the fitted center near the render origin.
    const double phase = std::remainder(absoluteOrigin, texelSize);
    return center - static_cast<float>(std::remainder(center + phase, texelSize));
}

} // namespace

bool CascadedShadowMap::ensureResources(ID3D11DeviceContext* context, unsigned resolution, unsigned count) {
    if (m_resolutionAttempted == resolution && m_countAttempted == count) {
        return m_shaderView.Get() != nullptr;
    }
    m_resolutionAttempted = resolution;
    m_countAttempted = count;
    m_shaderView.Reset();
    for (auto& view : m_depthViews) { view.Reset(); }

    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_BACK;
    rasterizer.DepthClipEnable = FALSE;
    rasterizer.DepthBias = 128;
    rasterizer.SlopeScaledDepthBias = 1.0f;
    rasterizer.DepthBiasClamp = 0.001f;

    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = texture.Height = resolution;
    texture.MipLevels = texture.SampleDesc.Count = 1;
    texture.ArraySize = count;
    texture.Format = DXGI_FORMAT_R32_TYPELESS;
    texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    const auto fail = [&](HRESULT error) {
        std::cerr << "Cannot create " << count << " directional shadow cascades at " << resolution
            << " pixels (HRESULT " << error << ")\n";
        return false;
    };
    HRESULT hr = S_OK;
    if (!m_sampler) {
        hr = device->CreateSamplerState(&sampler, m_sampler.GetAddressOf());
        if (FAILED(hr)) { return fail(hr); }
    }
    if (!m_rasterizer) {
        hr = device->CreateRasterizerState(&rasterizer, m_rasterizer.GetAddressOf());
        if (FAILED(hr)) { return fail(hr); }
    }
    ComPtr<ID3D11Texture2D> depth;
    hr = device->CreateTexture2D(&texture, nullptr, depth.GetAddressOf());
    if (FAILED(hr)) { return fail(hr); }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dsv.Texture2DArray.ArraySize = 1;
    decltype(m_depthViews) depthViews;
    for (unsigned i = 0; i < count; ++i) {
        dsv.Texture2DArray.FirstArraySlice = i;
        hr = device->CreateDepthStencilView(depth.Get(), &dsv, depthViews[i].GetAddressOf());
        if (FAILED(hr)) { return fail(hr); }
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srv.Texture2DArray.MipLevels = 1;
    srv.Texture2DArray.ArraySize = count;
    hr = device->CreateShaderResourceView(depth.Get(), &srv, m_shaderView.GetAddressOf());
    if (FAILED(hr)) { return fail(hr); }
    m_depthViews = std::move(depthViews);
    return true;
}

bool CascadedShadowMap::update(ID3D11DeviceContext* context, const Mat4& view, const Mat4& projection,
    DVec3 renderOrigin, Vec3 lightDirection, const SceneDrawList& settings) {
    const auto& p = projection.m;
    const bool orthographic = p[15] == 1.0f;
    const float nearDepth = -p[14] / p[10];
    const float cameraFar = orthographic ? (1.0f - p[14]) / p[10] :
        p[10] == 1.0f ? std::numeric_limits<float>::infinity() : p[14] / (1.0f - p[10]);
    const float distance = std::isfinite(settings.shadowDistance) ?
        std::clamp(settings.shadowDistance, 10.0f, 1000.0f) : 100.0f;
    const float farDepth = std::min(cameraFar, distance);
    if (!std::isfinite(nearDepth) || !(farDepth > nearDepth) || (!orthographic && nearDepth <= 0) ||
        !(p[0] > 0) || !(p[5] > 0) || !std::isfinite(dot(lightDirection, lightDirection))) {
        return false;
    }
    const auto resolution = static_cast<unsigned>(std::clamp(settings.shadowResolution, 512, 4096));
    const auto count = static_cast<unsigned>(std::clamp(settings.shadowCascadeCount, 1, static_cast<int>(kMaxCascades)));
    if (!ensureResources(context, resolution, count)) { return false; }

    const float lambda = std::isfinite(settings.shadowSplitLambda) ?
        std::clamp(settings.shadowSplitLambda, 0.0f, 1.0f) : 0.75f;
    m_nearDepth = nearDepth;
    const Vec3 up = std::abs(lightDirection.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Mat4 lightBasis = lookAt({}, lightDirection, up);
    m_lightX = {lightBasis.m[0], lightBasis.m[4], lightBasis.m[8]};
    m_lightY = {lightBasis.m[1], lightBasis.m[5], lightBasis.m[9]};
    m_lightZ = {lightBasis.m[2], lightBasis.m[6], lightBasis.m[10]};
    const double originX = lightCoordinate(m_lightX, renderOrigin);
    const double originY = lightCoordinate(m_lightY, renderOrigin);
    const Mat4 inverseView = inverse(view);
    const float size = static_cast<float>(resolution);

    float splitNear = nearDepth;
    float fitNear = nearDepth;
    for (unsigned i = 0; i < count; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(count);
        const float uniform = std::lerp(nearDepth, farDepth, t);
        const float split = orthographic || i + 1 == count ? uniform :
            std::lerp(uniform, nearDepth * std::pow(farDepth / nearDepth, t), lambda);

        // Unproject each slice plane at its view-space depth. Fit in view space
        // so rounding the radius is independent of camera rotation and translation.
        Vec3 corners[8];
        unsigned cornerIndex = 0;
        for (const float depth : {fitNear, split}) {
            const float w = orthographic ? 1.0f : depth;
            for (const float y : {-1.0f, 1.0f}) {
                for (const float x : {-1.0f, 1.0f}) {
                    corners[cornerIndex++] = {
                        (x * w - p[8] * depth - p[12]) / p[0],
                        (y * w - p[9] * depth - p[13]) / p[5],
                        depth,
                    };
                }
            }
        }
        Vec3 center{};
        for (const auto& corner : corners) { center = center + corner; }
        center = center * (1.0f / 8.0f);
        float radiusSquared = 0;
        for (const auto& corner : corners) {
            const Vec3 delta = corner - center;
            radiusSquared = std::max(radiusSquared, dot(delta, delta));
        }
        // Reserve three texels per edge for snapping, the receiver normal offset and PCF.
        constexpr float kRadiusSteps = 16.0f;
        const float paddedRadius = std::sqrt(radiusSquared) * size / (size - 6.0f);
        const float radius = std::max(1.0f, std::ceil(paddedRadius * kRadiusSteps)) / kRadiusSteps;
        const float texelSize = 2.0f * radius / size;
        const Vec3 lightCenter = lightSpaceCenter(transformPoint(inverseView, center));
        const Vec3 snappedCenter{snapToTexel(lightCenter.x, originX, texelSize),
            snapToTexel(lightCenter.y, originY, texelSize), lightCenter.z};

        const float casterMargin = radius;
        Mat4 lightView = lightBasis;
        lightView.m[12] = -snappedCenter.x;
        lightView.m[13] = -snappedCenter.y;
        lightView.m[14] = -snappedCenter.z + radius + casterMargin;
        Mat4 ortho;
        ortho.m[0] = ortho.m[5] = 1.0f / radius;
        ortho.m[10] = 1.0f / (2.0f * radius + casterMargin);
        m_cascades[i] = {ortho * lightView, snappedCenter, radius, radius, split, texelSize};

        // The next cascade is sampled before its logical split begins.
        fitNear = std::lerp(split, splitNear, kBlendFraction);
        splitNear = split;
    }
    return true;
}

Vec3 CascadedShadowMap::lightSpaceCenter(Vec3 center) const {
    return {dot(m_lightX, center), dot(m_lightY, center), dot(m_lightZ, center)};
}

bool CascadedShadowMap::overlaps(unsigned index, Vec3 lightCenter, float radius) const {
    const auto& cascade = m_cascades[index];
    const float reach = cascade.halfSize + radius;
    // Ignore the light-space near plane so upstream casters survive depth clamping.
    return std::abs(lightCenter.x - cascade.center.x) <= reach &&
        std::abs(lightCenter.y - cascade.center.y) <= reach &&
        lightCenter.z - cascade.center.z <= cascade.farOffset + radius;
}

} // namespace sat
