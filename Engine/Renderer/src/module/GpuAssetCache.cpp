#include "GpuAssetCache.hpp"

#include "Math.hpp"
#include "ShaderLayout.hpp"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sat {
namespace {
using Microsoft::WRL::ComPtr;
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
};
struct MeshResource {
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> indices;
    GpuMesh view;
};
struct TextureResource {
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11ShaderResourceView> srgb;
    std::size_t sampler = 0;
};
struct MaterialResource {
    ComPtr<ID3D11Buffer> constants;
    GpuMaterial view;
};
bool createMesh(ID3D11Device* device, const MeshData& data, MeshResource& resource, std::string& error) {
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.ByteWidth = static_cast<UINT>(data.vertices.size() * sizeof(MeshVertex));
    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = data.vertices.data();
    HRESULT hr = device->CreateBuffer(&desc, &initial, resource.vertices.GetAddressOf());
    if (SUCCEEDED(hr)) {
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        desc.ByteWidth = static_cast<UINT>(data.indices.size() * sizeof(std::uint32_t));
        initial.pSysMem = data.indices.data();
        hr = device->CreateBuffer(&desc, &initial, resource.indices.GetAddressOf());
    }
    if (FAILED(hr)) { error = "Cannot create GPU mesh buffers (HRESULT " + std::to_string(hr) + ")"; return false; }
    resource.view = {resource.vertices.Get(), resource.indices.Get(), static_cast<UINT>(data.indices.size()),
        static_cast<UINT>(sizeof(MeshVertex)), DXGI_FORMAT_R32_UINT};
    float extent[3];
    for (int axis = 0; axis < 3; ++axis) {
        const float center = std::midpoint(data.minimum[axis], data.maximum[axis]);
        resource.view.boundsCenter[axis] = center;
        extent[axis] = std::max(center - data.minimum[axis], data.maximum[axis] - center);
    }
    resource.view.boundsRadius = length({extent[0], extent[1], extent[2]});
    return true;
}
bool createTexture(ID3D11Device* device, const TextureData& data, TextureResource& resource, std::string& error) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(data.width);
    desc.Height = static_cast<UINT>(data.height);
    desc.MipLevels = static_cast<UINT>(data.mips.size() + 1);
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> initial(desc.MipLevels);
    initial[0].pSysMem = data.pixels.data();
    initial[0].SysMemPitch = static_cast<UINT>(data.width) * 4;
    for (std::size_t i = 0; i < data.mips.size(); ++i) {
        initial[i + 1].pSysMem = data.mips[i].pixels.data();
        initial[i + 1].SysMemPitch = static_cast<UINT>(data.mips[i].width) * 4;
    }
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, initial.data(), texture.GetAddressOf());
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = desc.MipLevels;
    if (SUCCEEDED(hr)) { hr = device->CreateShaderResourceView(texture.Get(), &view, resource.view.GetAddressOf()); }
    if (FAILED(hr)) { error = "Cannot create GPU texture (HRESULT " + std::to_string(hr) + ")"; return false; }
    resource.sampler = static_cast<std::size_t>(data.filter) * 2 + static_cast<std::size_t>(data.wrap);
    return true;
}
}
struct GpuAssetCache::Impl {
    AssetLibrary library;
    std::unordered_map<std::string, MeshResource, StringHash, std::equal_to<>> meshes;
    std::unordered_map<std::string, TextureResource, StringHash, std::equal_to<>> textures;
    std::array<ComPtr<ID3D11SamplerState>, 6> samplers;
    std::unordered_map<std::string, MaterialResource, StringHash, std::equal_to<>> materials;
    std::array<TextureResource, 3> defaults;
    bool defaultsReady = false;
    bool defaultsFailed = false;
    std::vector<std::string> errors;
    std::size_t reportedCpuErrors = 0;
    bool limitReported = false;

    void reportCpuErrors() {
        const auto& source = library.errors();
        for (; reportedCpuErrors < source.size(); ++reportedCpuErrors) { errors.push_back(source[reportedCpuErrors]); }
    }
    bool hasSpace() {
        if (meshes.size() + textures.size() + materials.size() < 4096) { return true; }
        if (!limitReported) { errors.emplace_back("GPU asset cache limit exceeded (4096 paths)"); limitReported = true; }
        return false;
    }
    bool ensureSampler(ID3D11Device* device, std::size_t index, std::string& error) {
        if (samplers[index]) { return true; }
        D3D11_SAMPLER_DESC desc{};
        const D3D11_FILTER filters[] = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_LINEAR};
        desc.Filter = filters[index / 2];
        desc.AddressU = desc.AddressV = desc.AddressW = index % 2 == 0 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        const HRESULT hr = device->CreateSamplerState(&desc, samplers[index].GetAddressOf());
        if (FAILED(hr)) { error = "Cannot create texture sampler (HRESULT " + std::to_string(hr) + ")"; return false; }
        return true;
    }
    void loadMesh(ID3D11Device* device, std::string_view path) {
        if (meshes.contains(path) || !hasSpace()) { return; }
        auto& destination = meshes.try_emplace(std::string(path)).first->second;
        const auto* data = library.mesh(path);
        if (data == nullptr) { reportCpuErrors(); return; }
        MeshResource candidate;
        std::string error;
        if (!createMesh(device, *data, candidate, error)) { errors.push_back(std::string(path) + ": " + error); return; }
        destination = std::move(candidate);
    }
    void loadTexture(ID3D11Device* device, std::string_view path) {
        if (textures.contains(path) || !hasSpace()) { return; }
        auto& destination = textures.try_emplace(std::string(path)).first->second;
        const auto* data = library.texture(path);
        if (data == nullptr) { reportCpuErrors(); return; }
        TextureResource candidate;
        std::string error;
        if (!createTexture(device, *data, candidate, error) || !ensureSampler(device, candidate.sampler, error)) {
            errors.push_back(std::string(path) + ": " + error); return;
        }
        destination = std::move(candidate);
    }
    bool ensureDefaults(ID3D11Device* device) {
        if (defaultsReady || defaultsFailed) { return defaultsReady; }
        const unsigned char pixels[3][4] = {{255, 255, 255, 255}, {128, 128, 255, 255}, {0, 0, 0, 255}};
        std::string error;
        TextureData data;
        data.width = data.height = 1;
        for (std::size_t i = 0; i < defaults.size(); ++i) {
            data.pixels.assign(pixels[i], pixels[i] + 4);
            if (!createTexture(device, data, defaults[i], error)) {
                defaultsFailed = true;
                errors.push_back("Default material: " + error);
                return false;
            }
        }
        defaultsReady = true;
        return true;
    }
    ID3D11ShaderResourceView* colorView(ID3D11Device* device, TextureResource& texture) {
        if (!texture.view) { return nullptr; }
        if (!texture.srgb) {
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            texture.view->GetDesc(&desc);
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            ComPtr<ID3D11Resource> resource;
            texture.view->GetResource(resource.GetAddressOf());
            if (FAILED(device->CreateShaderResourceView(resource.Get(), &desc, texture.srgb.GetAddressOf()))) {
                return nullptr;
            }
        }
        return texture.srgb.Get();
    }
    void loadMaterial(ID3D11Device* device, std::string_view path) {
        if (materials.contains(path) || (!path.empty() && !hasSpace()) || !ensureDefaults(device)) { return; }
        auto& material = materials.try_emplace(std::string(path)).first->second;
        const MaterialData fallback;
        const auto* data = path.empty() ? &fallback : library.material(path);
        if (!data) { reportCpuErrors(); return; }

        std::size_t samplerIndex = 4;
        ID3D11ShaderResourceView* views[] = {
            defaults[0].view.Get(), defaults[0].view.Get(), defaults[1].view.Get(), defaults[2].view.Get()
        };
        const std::string* paths[] = {&data->baseColorTexture, &data->metallicRoughnessTexture,
            &data->normalTexture, &data->emissiveTexture};
        bool hasEmissiveTexture = false;
        bool samplerSelected = false;
        for (std::size_t i = 0; i < std::size(paths); ++i) {
            if (paths[i]->empty()) { continue; }
            loadTexture(device, *paths[i]);
            const auto found = textures.find(*paths[i]);
            if (found == textures.end() || !found->second.view) { continue; }
            auto* view = i == 0 || i == 3 ? colorView(device, found->second) : found->second.view.Get();
            if (!view) {
                errors.push_back(*paths[i] + ": cannot create sRGB texture view");
                continue;
            }
            views[i] = view;
            hasEmissiveTexture |= i == 3;
            if (!samplerSelected) {
                samplerIndex = found->second.sampler;
                samplerSelected = true;
            }
        }
        std::string error;
        if (!ensureSampler(device, samplerIndex, error)) {
            errors.push_back(std::string(path) + ": " + error);
            return;
        }
        const MaterialCb parameters{
            {data->baseColor[0], data->baseColor[1], data->baseColor[2], data->baseColor[3]},
            {data->emissive[0], data->emissive[1], data->emissive[2]}, data->normalScale,
            data->metallic, data->roughness, hasEmissiveTexture ? 1.0f : 0.0f,
            data->shader == MaterialShader::Unlit ? 1.0f : 0.0f,
            static_cast<float>(data->alphaMode), data->alphaCutoff, data->specularStrength, 0.0f
        };
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(parameters);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = &parameters;
        if (FAILED(device->CreateBuffer(&desc, &initial, material.constants.GetAddressOf()))) {
            errors.push_back(std::string(path) + ": cannot create material constant buffer");
            return;
        }
        material.view = {data->alphaMode, data->shaderPath, material.constants.Get(), {views[0], views[1], views[2], views[3]}, samplers[samplerIndex].Get()};
    }
};
GpuAssetCache::GpuAssetCache() : m_impl(std::make_unique<Impl>()) {}
GpuAssetCache::~GpuAssetCache() = default;
void GpuAssetCache::setReader(AssetReader reader) {
    m_impl->materials.clear(); m_impl->defaults = {};
    m_impl->defaultsReady = m_impl->defaultsFailed = false;
    m_impl->meshes.clear(); m_impl->textures.clear(); m_impl->samplers = {};
    m_impl->errors.clear(); m_impl->reportedCpuErrors = 0; m_impl->limitReported = false;
    m_impl->library.setReader(std::move(reader));
}
void GpuAssetCache::prepare(ID3D11Device* device, const SceneDrawList& drawList) {
    if (device == nullptr || drawList.meshes == nullptr) { return; }
    if (!drawList.environmentPath.empty()) {
        m_impl->loadTexture(device, drawList.environmentPath);
        const auto found = m_impl->textures.find(drawList.environmentPath);
        if (found != m_impl->textures.end()) { (void)m_impl->colorView(device, found->second); }
    }
    m_impl->loadMaterial(device, {});
    for (int i = 0; i < drawList.meshCount; ++i) {
        const auto& instance = drawList.meshes[i];
        if (!instance.meshPath.empty()) { m_impl->loadMesh(device, instance.meshPath); }
        if (!instance.materialPath.empty()) { m_impl->loadMaterial(device, instance.materialPath); }
    }
}
ID3D11ShaderResourceView* GpuAssetCache::environment(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found == m_impl->textures.end() ? nullptr : found->second.srgb.Get();
}

bool GpuAssetCache::reimport(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
    const AssetImportWriter& writer, std::string& error) {
    if (!device) { error = "No graphics device for asset import"; return false; }
    m_impl->reportCpuErrors();
    const bool exists = isMeshAssetPath(path) ? m_impl->meshes.contains(path) : m_impl->textures.contains(path);
    if (!exists && !m_impl->hasSpace()) { error = "GPU asset cache limit exceeded"; return false; }
    PreparedAssetImport prepared;
    if (!m_impl->library.stage(path, settings, prepared, error)) { return false; }
    const auto cancel = [&] {
        m_impl->library.discard(prepared);
        if (!exists) {
            if (prepared.isMesh) { m_impl->meshes.erase(std::string(path)); }
            else { m_impl->textures.erase(std::string(path)); }
        }
        return false;
    };
    MeshResource mesh;
    TextureResource texture;
    if (prepared.isMesh) {
        if (!createMesh(device, prepared.mesh, mesh, error)) { return cancel(); }
        m_impl->meshes.try_emplace(prepared.path);
    } else {
        if (!createTexture(device, prepared.texture, texture, error) || !m_impl->ensureSampler(device, texture.sampler, error)) { return cancel(); }
        m_impl->textures.try_emplace(prepared.path);
    }
    // All decoding, GPU allocations and map growth precede persistence. A failed
    // writer leaves active CPU/GPU resources untouched. Commit only moves data.
    if (writer && !writer(prepared.settings, error)) { return cancel(); }
    if (prepared.isMesh) { m_impl->meshes.find(path)->second = std::move(mesh); }
    else {
        m_impl->materials.clear();
        m_impl->textures.find(path)->second = std::move(texture);
    }
    m_impl->library.commit(std::move(prepared));
    m_impl->reportedCpuErrors = m_impl->library.errors().size();
    std::erase_if(m_impl->errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
    return true;
}
void GpuAssetCache::invalidateMaterial(std::string_view path) {
    if (const auto found = m_impl->materials.find(path); found != m_impl->materials.end()) {
        m_impl->materials.erase(found);
    }
    m_impl->library.invalidateMaterial(path);
    m_impl->reportedCpuErrors = m_impl->library.errors().size();
    std::erase_if(m_impl->errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
}
const GpuMaterial* GpuAssetCache::material(std::string_view path, bool fallback) const {
    auto found = m_impl->materials.find(path);
    if (fallback && (found == m_impl->materials.end() || !found->second.constants)) { found = m_impl->materials.find(""); }
    return found != m_impl->materials.end() && found->second.constants ? &found->second.view : nullptr;
}
const GpuMesh* GpuAssetCache::mesh(std::string_view path) const {
    const auto found = m_impl->meshes.find(path);
    return found != m_impl->meshes.end() && found->second.view.valid() ? &found->second.view : nullptr;
}
ID3D11ShaderResourceView* GpuAssetCache::texture(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found != m_impl->textures.end() ? found->second.view.Get() : nullptr;
}
ID3D11SamplerState* GpuAssetCache::sampler(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found != m_impl->textures.end() && found->second.view ? m_impl->samplers[found->second.sampler].Get() : nullptr;
}
bool GpuAssetCache::bounds(std::string_view path, Vec3& minimum, Vec3& maximum) {
    const auto* mesh = m_impl->library.mesh(path);
    m_impl->reportCpuErrors();
    if (!mesh) { return false; }
    minimum = {mesh->minimum[0], mesh->minimum[1], mesh->minimum[2]};
    maximum = {mesh->maximum[0], mesh->maximum[1], mesh->maximum[2]};
    return true;
}
std::string GpuAssetCache::info(std::string_view path) {
    std::string result;
    if (isMeshAssetPath(path)) {
        if (const auto* data = m_impl->library.mesh(path)) {
            result = std::to_string(data->vertices.size()) + " vertices, " + std::to_string(data->indices.size() / 3) + " triangles";
        }
    } else if (isTextureAssetPath(path)) {
        if (const auto* data = m_impl->library.texture(path)) {
            result = std::to_string(data->width) + " x " + std::to_string(data->height) + ", " + std::to_string(data->mips.size() + 1) + " mip levels";
        }
    }
    m_impl->reportCpuErrors();
    return result;
}
const std::vector<std::string>& GpuAssetCache::errors() const { return m_impl->errors; }
}
