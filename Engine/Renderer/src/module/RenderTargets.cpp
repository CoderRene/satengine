#include "RenderState.hpp"

#include <iostream>

namespace sat {

void RenderSession::releaseOffscreenView(OffscreenView& view) {
    view.bloomLevels.clear();
    view.bloomAllocationFailed = false;
    view.hdrTarget.Reset();
    view.hdrView.Reset();
    releaseCom(view.dsv);
    releaseCom(view.depth);
    releaseCom(view.srv);
    releaseCom(view.rtv);
    releaseCom(view.color);
    view.width = 0;
    view.height = 0;
}

void RenderSession::resizeOffscreenView(
    OffscreenView& view, int width, int height, const char* label) {
    if (!valid() || width < 1 || height < 1) {
        return;
    }
    if (width == view.width && height == view.height && view.srv != nullptr && view.dsv != nullptr) {
        return;
    }

    releaseOffscreenView(view);

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = static_cast<UINT>(width);
    colorDesc.Height = static_cast<UINT>(height);
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_state->device->CreateTexture2D(&colorDesc, nullptr, &view.color);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color target\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_state->device->CreateRenderTargetView(view.color, nullptr, &view.rtv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color RTV\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_state->device->CreateShaderResourceView(view.color, nullptr, &view.srv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " color SRV\n";
        releaseOffscreenView(view);
        return;
    }

    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hdr;
    if (FAILED(m_state->device->CreateTexture2D(&colorDesc, nullptr, hdr.GetAddressOf())) ||
        FAILED(m_state->device->CreateRenderTargetView(hdr.Get(), nullptr, view.hdrTarget.GetAddressOf())) ||
        FAILED(m_state->device->CreateShaderResourceView(hdr.Get(), nullptr, view.hdrView.GetAddressOf()))) {
        std::cerr << "Failed to create " << label << " HDR target\n";
        releaseOffscreenView(view);
        return;
    }
    D3D11_TEXTURE2D_DESC depthDesc = colorDesc;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = m_state->device->CreateTexture2D(&depthDesc, nullptr, &view.depth);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " depth target\n";
        releaseOffscreenView(view);
        return;
    }

    hr = m_state->device->CreateDepthStencilView(view.depth, nullptr, &view.dsv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create " << label << " depth view\n";
        releaseOffscreenView(view);
        return;
    }

    view.width = width;
    view.height = height;
}

} // namespace sat
