#include "RenderState.hpp"

#include "AssetData.hpp"
#include "Math.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace sat {

std::uintptr_t RenderSession::materialThumbnail(std::string_view path) {
    if (m_state->thumbnailsDirty || !isMaterialAssetPath(path)) { return 0; }
    std::string key(path);
    auto& cache = m_state->thumbnails;
    if (const auto found = cache.find(key); found != cache.end()) {
        found->second.lastUsed = m_state->thumbnailFrame;
        return reinterpret_cast<std::uintptr_t>(found->second.view.Get());
    }
    if (m_state->thumbnailBudget == 0 || !valid() || !initializeShaders()) { return 0; }
    constexpr std::size_t kCacheLimit = 256;
    if (cache.size() >= kCacheLimit) {
        const auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto& a, const auto& b) {
            return a.second.lastUsed < b.second.lastUsed;
        });
        if (oldest->second.lastUsed == m_state->thumbnailFrame) { return 0; }
        cache.erase(oldest);
    }
    --m_state->thumbnailBudget;

    constexpr int kSize = 96;
    auto& preview = m_state->thumbnail;
    resizeOffscreenView(preview, kSize, kSize, "material thumbnail");
    if (!preview.rtv || !m_state->meshPass.ensure(m_state->device)) { return 0; }

    GameViewCamera camera;
    camera.clearColor[0] = 0.16f;
    camera.clearColor[1] = 0.17f;
    camera.clearColor[2] = 0.19f;
    const auto view = lookAt({2.2f, 1.7f, -2.5f}, {}, {0, 1, 0});
    const auto projection = perspective(32, 1, 0.1f, 10);
    std::memcpy(camera.view, view.m, sizeof(camera.view));
    std::memcpy(camera.projection, projection.m, sizeof(camera.projection));
    MeshInstance mesh;
    mesh.materialPath = path;
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
    m_state->meshPass.prepareAssets(m_state->device, draw);
    auto& entry = cache.try_emplace(std::move(key)).first->second;
    entry.lastUsed = m_state->thumbnailFrame;
    if (!m_state->meshPass.material(path)) { return 0; }
    renderOffscreenView(preview, camera, draw);

    // Snapshot the shared target. ImGui consumes the cached SRV at end of frame.
    D3D11_TEXTURE2D_DESC desc{};
    preview.color->GetDesc(&desc);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> image;
    if (FAILED(m_state->device->CreateTexture2D(&desc, nullptr, image.GetAddressOf())) ||
        FAILED(m_state->device->CreateShaderResourceView(image.Get(), nullptr, entry.view.GetAddressOf()))) {
        return 0;
    }
    m_state->context->OMSetRenderTargets(0, nullptr, nullptr);
    m_state->context->CopyResource(image.Get(), preview.color);
    return reinterpret_cast<std::uintptr_t>(entry.view.Get());
}

} // namespace sat
