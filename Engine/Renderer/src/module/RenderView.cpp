#include "RenderState.hpp"

#include "Math.hpp"
#include "ShaderLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

namespace sat {

void RenderSession::renderOffscreenView(
    OffscreenView& view, const GameViewCamera& camera, const SceneDrawList& drawList) {
    if (view.rtv == nullptr || view.dsv == nullptr || m_state->context == nullptr) {
        return;
    }

    D3D11_VIEWPORT viewport{};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(view.width);
    viewport.Height = static_cast<float>(view.height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_state->context->RSSetViewports(1, &viewport);
    m_state->context->OMSetRenderTargets(1, &view.rtv, view.dsv);
    m_state->context->ClearRenderTargetView(view.rtv, camera.clearColor);
    m_state->context->ClearDepthStencilView(view.dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    Mat4 viewMat{};
    Mat4 projMat{};
    std::memcpy(viewMat.m, camera.view, sizeof(viewMat.m));
    std::memcpy(projMat.m, camera.projection, sizeof(projMat.m));
    const Mat4 viewProj = projMat * viewMat;

    if (!initializeShaders()) { return; }
    if (!m_state->postSampler) {
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler.MaxAnisotropy = 1;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_state->device->CreateSamplerState(&sampler, m_state->postSampler.GetAddressOf()))) {
            return;
        }
    }

    auto* context = m_state->context;
    if (!m_state->bloomCb) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(BloomCb);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(m_state->device->CreateBuffer(&desc, nullptr, m_state->bloomCb.GetAddressOf()))) {
            std::cerr << "Cannot create bloom constant buffer\n";
            return;
        }
    }
    auto* hdrTarget = view.hdrTarget.Get();
    context->OMSetRenderTargets(1, &hdrTarget, view.dsv);
    // Camera clear colors are authored in display space. Invert the display transform.
    float hdrClear[4]{};
    for (int i = 0; i < 3; ++i) {
        const float srgb = std::clamp(camera.clearColor[i], 0.0f, 1.0f);
        const float linear = srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        hdrClear[i] = linear / std::max(1.0f - linear, 0.001f);
    }
    context->ClearRenderTargetView(hdrTarget, hdrClear);
    if (drawList.meshCount > 0 && m_state->meshPass.ensure(m_state->device)) {
        const Mat4 invView = inverse(viewMat);
        m_state->meshPass.prepareAssets(m_state->device, drawList);
        m_state->meshPass.draw(context, m_state->shaders.program(ShaderId::Mesh), viewProj,
            {invView.m[12], invView.m[13], invView.m[14]}, drawList, true, m_state->shaders.program(ShaderId::Shadow).pixel, &m_state->shaders);
    }

    const auto post = m_state->shaders.program(ShaderId::PostProcess);
    context->OMSetRenderTargets(1, &view.rtv, nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetState(nullptr);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(post.vertex, nullptr, 0);
    context->PSSetShader(post.pixel, nullptr, 0);
    auto* sampler = m_state->postSampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    const auto& bloom = drawList.bloom;
    BloomCb constants{bloom.threshold, bloom.softKnee * bloom.threshold,
        bloom.enabled ? bloom.intensity : 0.0f, bloom.radius, {bloom.tint[0], bloom.tint[1], bloom.tint[2]},
        bloom.highlightClamp, static_cast<unsigned>(std::clamp(bloom.quality, 0, 2) + 1), 0, 0, 0};
    auto* bloomBuffer = m_state->bloomCb.Get();
    context->PSSetConstantBuffers(0, 1, &bloomBuffer);
    const auto drawPost = [&](ID3D11RenderTargetView* target, ID3D11ShaderResourceView* source,
        ID3D11ShaderResourceView* secondary, int width, int height) {
        const D3D11_VIEWPORT passViewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        context->RSSetViewports(1, &passViewport);
        context->OMSetRenderTargets(1, &target, nullptr);
        ID3D11ShaderResourceView* inputs[]{source, secondary};
        context->PSSetShaderResources(0, 2, inputs);
        context->UpdateSubresource(bloomBuffer, 0, nullptr, &constants, 0, 0);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* unbound[2]{};
        context->PSSetShaderResources(0, 2, unbound);
    };

    ID3D11ShaderResourceView* glow = nullptr;
    if (constants.intensity > 0 && bloom.radius > 0 && bloom.highlightClamp > 0) {
        // Radius selects the deepest scale. Blend in the last level as radius increases.
        const float scale = std::clamp(std::log2(std::max(bloom.radius, 2.0f) / 2.0f), 0.0f, 5.0f);
        const auto wanted = static_cast<std::size_t>(std::ceil(scale)) + 1;
        int width = view.width, height = view.height;
        std::size_t count = 0;
        for (; count < wanted; ++count) {
            width = (width + 1) / 2;
            height = (height + 1) / 2;
            if (count == view.bloomLevels.size()) {
                if (view.bloomAllocationFailed) { break; }
                OffscreenView::BloomLevel level;
                level.width = width;
                level.height = height;
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = static_cast<UINT>(width);
                desc.Height = static_cast<UINT>(height);
                desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
                desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                const auto create = [&](auto& target, auto& resource) {
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                    return SUCCEEDED(m_state->device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) &&
                        SUCCEEDED(m_state->device->CreateRenderTargetView(texture.Get(), nullptr, target.GetAddressOf())) &&
                        SUCCEEDED(m_state->device->CreateShaderResourceView(texture.Get(), nullptr, resource.GetAddressOf()));
                };
                if (!create(level.downTarget, level.downView) || !create(level.upTarget, level.upView)) {
                    std::cerr << "Cannot allocate bloom pyramid level\n";
                    view.bloomAllocationFailed = true;
                    break;
                }
                view.bloomLevels.push_back(std::move(level));
            }
            auto& level = view.bloomLevels[count];
            constants.pass = count == 0 ? 1u : 2u;
            drawPost(level.downTarget.Get(), count == 0 ? view.hdrView.Get() :
                view.bloomLevels[count - 1].downView.Get(), nullptr, width, height);
            if (width == 1 && height == 1) { ++count; break; }
        }
        if (count != 0) {
            glow = view.bloomLevels[count - 1].downView.Get();
            constants.pass = 3;
            for (std::size_t i = count - 1; i > 0; --i) {
                auto& level = view.bloomLevels[i - 1];
                constants.coarseWeight = 0.5f * std::clamp(scale - static_cast<float>(i - 1), 0.0f, 1.0f);
                drawPost(level.upTarget.Get(), level.downView.Get(), glow, level.width, level.height);
                glow = level.upView.Get();
            }
        }
    }
    constants.pass = 0;
    if (!glow) { constants.intensity = 0; }
    drawPost(view.rtv, view.hdrView.Get(), glow, view.width, view.height);
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &view.rtv, view.dsv);

    if (drawList.drawGrid && ensureDebugLines()) {
        const auto program = m_state->shaders.program(ShaderId::Grid);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(
                m_state->context->Map(m_state->lineCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const Mat4 gridViewProj = viewProj * translationMatrix(drawList.gridOffset);
            std::memcpy(mapped.pData, gridViewProj.m, sizeof(gridViewProj.m));
            m_state->context->Unmap(m_state->lineCb, 0);

            const UINT stride = sizeof(DebugLineVertex);
            const UINT offset = 0;
            m_state->context->IASetInputLayout(program.layout);
            m_state->context->IASetVertexBuffers(0, 1, &m_state->lineVb, &stride, &offset);
            m_state->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            m_state->context->VSSetShader(program.vertex, nullptr, 0);
            m_state->context->VSSetConstantBuffers(0, 1, &m_state->lineCb);
            m_state->context->PSSetShader(program.pixel, nullptr, 0);
            m_state->context->RSSetState(m_state->lineRasterizer);
            m_state->context->OMSetDepthStencilState(m_state->lineDepthState, 0);
            m_state->context->Draw(m_state->lineVertexCount, 0);
        }
    }

    if (drawList.drawEditorOverlay && drawList.selectedMesh >= 0 &&
        drawList.selectedMesh < drawList.meshCount && drawList.meshes != nullptr) {
        const MeshInstance& selected = drawList.meshes[drawList.selectedMesh];
        const GpuMesh* meshGeometry = m_state->meshPass.geometry(selected);
        if (meshGeometry != nullptr && m_state->outlinePass.ensure(m_state->device)) {
            m_state->outlinePass.draw(m_state->context, m_state->shaders.program(ShaderId::SelectionOutline), viewProj, selected, *meshGeometry);
        }
    }

    if (drawList.drawEditorOverlay && (drawList.frustumCount > 0 || drawList.debugLineCount > 0) &&
        m_state->frustumPass.ensure(m_state->device)) {
        m_state->frustumPass.draw(
            m_state->context, m_state->shaders.program(ShaderId::CameraFrustum), viewProj, drawList.frustums, drawList.frustumCount, drawList.debugLines, drawList.debugLineCount);
    }

    if (drawList.drawEditorOverlay && drawList.showTransformGizmo &&
        m_state->gizmoPass.ensure(m_state->device)) {
        const float scale = translateGizmoScale(drawList.gizmo.origin, viewMat, projMat,
            static_cast<float>(view.height));
        if (scale > 0.0f) {
            m_state->gizmoPass.draw(m_state->context, m_state->shaders.program(ShaderId::TransformGizmo), viewProj, drawList.gizmo, scale);
        }
    }
}

} // namespace sat
