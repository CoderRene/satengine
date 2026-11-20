#include "RenderState.hpp"

#include "AssetData.hpp"
#include "Math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace sat {

bool RenderSession::makeThumbnailSpace() {
    auto& cache = m_state->thumbnails;
    constexpr std::size_t kCacheLimit = 256;
    if (cache.size() >= kCacheLimit) {
        const auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto& a, const auto& b) {
            return a.second.lastUsed < b.second.lastUsed;
        });
        if (oldest->second.lastUsed == m_state->thumbnailFrame) { return false; }
        cache.erase(oldest);
    }
    return true;
}

void RenderSession::uploadTextureThumbnail(std::string path, const TextureData& image) {
    auto& entry = m_state->thumbnails.try_emplace(std::move(path)).first->second;
    entry.lastUsed = m_state->thumbnailFrame;
    if (image.pixels.empty()) { return; }

    const auto texture = createTexture(image.pixels.data(), image.width, image.height);
    entry.view.Attach(reinterpret_cast<ID3D11ShaderResourceView*>(texture));
    entry.aspectRatio = static_cast<float>(image.width) / static_cast<float>(image.height);
}

void RenderSession::uploadReadyThumbnails() {
    // Limit main-thread work to one preview per frame.
    auto ready = m_state->thumbnailLoader.takeReady();
    if (!ready || !makeThumbnailSpace()) { return; }
    if (ready->meshPass) {
        renderThumbnail(std::move(ready->path), *ready->meshPass);
    } else {
        uploadTextureThumbnail(std::move(ready->path), ready->image);
    }
}

AssetThumbnail RenderSession::assetThumbnail(std::string_view path, std::span<const Mat4> palette) {
    if (!palette.empty() && isSceneModelAssetPath(path) && valid()) {
        MeshInstance mesh; mesh.meshPath = path;
        SceneDrawList draw; draw.meshes = &mesh; draw.meshCount = 1;
        m_state->meshPass.prepareAssets(m_state->device, draw);
        renderThumbnail(std::string(path), m_state->meshPass, palette);
    }
    if (m_state->thumbnailsDirty || !valid()) { return {}; }
    if (!isTextureAssetPath(path) && !isMeshAssetPath(path) && !isMaterialAssetPath(path)) { return {}; }
    auto& cache = m_state->thumbnails;
    if (const auto found = cache.find(std::string(path)); found != cache.end()) {
        found->second.lastUsed = m_state->thumbnailFrame;
        return {reinterpret_cast<std::uintptr_t>(found->second.view.Get()), found->second.aspectRatio};
    }
    (void)m_state->thumbnailLoader.request(path);
    return {};
}

void RenderSession::renderThumbnail(std::string path, MeshPass& pass, std::span<const Mat4> palette) {
    const bool isMesh = isMeshAssetPath(path);
    auto& entry = m_state->thumbnails.try_emplace(path).first->second;
    entry.lastUsed = m_state->thumbnailFrame;
    if (!initializeShaders()) { return; }
    constexpr int kSize = 96;
    auto& preview = m_state->thumbnail;
    resizeOffscreenView(preview, kSize, kSize, "asset thumbnail");
    if (!preview.rtv) { return; }

    GameViewCamera camera;
    camera.clearColor[0] = 0.16f;
    camera.clearColor[1] = 0.17f;
    camera.clearColor[2] = 0.19f;
    const auto view = lookAt({2.2f, 1.7f, -2.5f}, {}, {0, 1, 0});
    const auto projection = perspective(32, 1, 0.1f, 10);
    std::memcpy(camera.view, view.m, sizeof(camera.view));
    std::memcpy(camera.projection, projection.m, sizeof(camera.projection));
    MeshInstance mesh;
    mesh.palette = palette.data(); mesh.paletteCount = static_cast<std::uint32_t>(palette.size());
    if (isMesh) {
        mesh.meshPath = path;
    } else {
        mesh.materialPath = path;
    }
    const LightInstance lights[] = {
        {{}, normalize({0.3f, -0.65f, 1.0f}), {3.5f, 3.2f, 2.8f}},
        {{}, normalize({-1.0f, -0.25f, 0.15f}), {1.1f, 1.4f, 2.1f}},
    };
    SceneDrawList draw;
    draw.meshes = &mesh;
    draw.meshCount = 1;
    draw.lights = lights;
    draw.lightCount = 2;
    draw.directionalShadows = false;
    draw.bloom.enabled = false;
    if (!pass.material(mesh.materialPath)) { return; }
    if (isMesh) {
        const auto* geometry = pass.geometry(mesh);
        if (geometry == nullptr || !std::isfinite(geometry->boundsRadius) ||
            geometry->boundsRadius <= 0.0f) { return; }

        // Center the mesh and fit its bounding sphere into the fixed preview camera.
        const float scale = 0.85f / geometry->boundsRadius;
        mesh.world = scalingMatrix({scale, scale, scale});
        for (int axis = 0; axis < 3; ++axis) {
            const float offset = -geometry->boundsCenter[axis] * scale;
            if (!std::isfinite(offset)) { return; }
            mesh.world.m[12 + axis] = offset;
        }
    }
    renderOffscreenView(preview, camera, draw, &pass);

    // Snapshot the shared target. ImGui consumes the cached SRV at end of frame.
    D3D11_TEXTURE2D_DESC desc{};
    preview.color->GetDesc(&desc);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (entry.view) {
        Microsoft::WRL::ComPtr<ID3D11Resource> image;
        entry.view->GetResource(image.GetAddressOf());
        m_state->context->OMSetRenderTargets(0, nullptr, nullptr);
        m_state->context->CopyResource(image.Get(), preview.color);
        return;
    }
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    if (FAILED(m_state->device->CreateTexture2D(&desc, nullptr, image.GetAddressOf())) ||
        FAILED(m_state->device->CreateShaderResourceView(image.Get(), nullptr, entry.view.GetAddressOf()))) {
        return;
    }
    m_state->context->OMSetRenderTargets(0, nullptr, nullptr);
    m_state->context->CopyResource(image.Get(), preview.color);
}

} // namespace sat
