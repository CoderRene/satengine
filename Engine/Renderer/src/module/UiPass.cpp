#include "UiPass.hpp"

#include "AssetData.hpp"
#include "GameUi.hpp"
#include "ShaderLayout.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace sat {
namespace {

using Microsoft::WRL::ComPtr;

struct Geometry {
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> indices;
    UINT indexCount = 0;
};

// The engine uses the immediate context, one viewport, and no shader class
// instances. Save the bindings touched by this pass, including the output MRTs.
class ScopedUiState {
public:
    explicit ScopedUiState(ID3D11DeviceContext& context) : m_context(context) {
        context.OMGetRenderTargets(static_cast<UINT>(m_targets.size()), m_targets.data(), m_dsv.GetAddressOf());
        context.OMGetBlendState(m_blend.GetAddressOf(), m_blendFactors, &m_sampleMask);
        context.OMGetDepthStencilState(m_depth.GetAddressOf(), &m_stencilRef);
        context.RSGetState(m_rasterizer.GetAddressOf());
        context.RSGetViewports(&m_viewportCount, m_viewports.data());
        context.RSGetScissorRects(&m_scissorCount, m_scissors.data());
        context.IAGetInputLayout(m_layout.GetAddressOf());
        context.IAGetPrimitiveTopology(&m_topology);
        context.IAGetVertexBuffers(0, 1, m_vertices.GetAddressOf(), &m_stride, &m_vertexOffset);
        context.IAGetIndexBuffer(m_indices.GetAddressOf(), &m_indexFormat, &m_indexOffset);
        context.VSGetShader(m_vs.GetAddressOf(), nullptr, nullptr);
        context.VSGetConstantBuffers(0, 1, m_constants.GetAddressOf());
        context.VSGetConstantBuffers(1, 1, m_vsExtra.GetAddressOf());
        context.PSGetConstantBuffers(0, 1, m_psExtra.GetAddressOf());
        context.PSGetShader(m_ps.GetAddressOf(), nullptr, nullptr);
        context.PSGetSamplers(0, 1, m_sampler.GetAddressOf());
        context.PSGetShaderResources(0, 1, m_texture.GetAddressOf());
        context.PSGetShaderResources(1, 1, m_secondary.GetAddressOf());
    }

    ~ScopedUiState() {
        UINT count = static_cast<UINT>(m_targets.size());
        while (count && !m_targets[count - 1]) { --count; }
        m_context.OMSetRenderTargets(count, count ? m_targets.data() : nullptr, m_dsv.Get());
        for (auto* target : m_targets) {
            if (target) { target->Release(); }
        }
        m_context.OMSetBlendState(m_blend.Get(), m_blendFactors, m_sampleMask);
        m_context.OMSetDepthStencilState(m_depth.Get(), m_stencilRef);
        m_context.RSSetState(m_rasterizer.Get());
        m_context.RSSetViewports(m_viewportCount, m_viewports.data());
        m_context.RSSetScissorRects(m_scissorCount, m_scissors.data());
        m_context.IASetInputLayout(m_layout.Get());
        m_context.IASetPrimitiveTopology(m_topology);
        m_context.IASetVertexBuffers(0, 1, m_vertices.GetAddressOf(), &m_stride, &m_vertexOffset);
        m_context.IASetIndexBuffer(m_indices.Get(), m_indexFormat, m_indexOffset);
        m_context.VSSetShader(m_vs.Get(), nullptr, 0);
        m_context.VSSetConstantBuffers(0, 1, m_constants.GetAddressOf());
        m_context.VSSetConstantBuffers(1, 1, m_vsExtra.GetAddressOf());
        m_context.PSSetConstantBuffers(0, 1, m_psExtra.GetAddressOf());
        m_context.PSSetShader(m_ps.Get(), nullptr, 0);
        m_context.PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        m_context.PSSetShaderResources(0, 1, m_texture.GetAddressOf());
        m_context.PSSetShaderResources(1, 1, m_secondary.GetAddressOf());
    }

    ScopedUiState(const ScopedUiState&) = delete;
    ScopedUiState& operator=(const ScopedUiState&) = delete;

private:
    ID3D11DeviceContext& m_context;
    std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> m_targets{};
    ComPtr<ID3D11DepthStencilView> m_dsv;
    ComPtr<ID3D11BlendState> m_blend;
    ComPtr<ID3D11DepthStencilState> m_depth;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11InputLayout> m_layout;
    ComPtr<ID3D11Buffer> m_vertices, m_indices, m_constants, m_vsExtra, m_psExtra;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11ShaderResourceView> m_texture, m_secondary;
    std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> m_viewports{};
    std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> m_scissors{};
    UINT m_viewportCount = static_cast<UINT>(m_viewports.size());
    UINT m_scissorCount = static_cast<UINT>(m_scissors.size());
    D3D11_PRIMITIVE_TOPOLOGY m_topology{};
    DXGI_FORMAT m_indexFormat{};
    float m_blendFactors[4]{};
    UINT m_sampleMask = 0;
    UINT m_stencilRef = 0;
    UINT m_stride = 0;
    UINT m_vertexOffset = 0;
    UINT m_indexOffset = 0;
};

ComPtr<ID3D11ShaderResourceView> createTexture(ID3D11Device& device, Rml::Span<const Rml::byte> pixels,
    Rml::Vector2i dimensions) {
    constexpr int kMaxDimension = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    if (dimensions.x < 1 || dimensions.x > kMaxDimension || dimensions.y < 1 || dimensions.y > kMaxDimension ||
        pixels.size() != static_cast<std::size_t>(dimensions.x) * dimensions.y * 4) {
        return {};
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(dimensions.x);
    desc.Height = static_cast<UINT>(dimensions.y);
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), desc.Width * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device.CreateTexture2D(&desc, &data, texture.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
        std::clog << "Game UI: failed to upload a D3D11 texture\n";
        return {};
    }
    return view;
}

bool createBuffer(ID3D11Device& device, const void* bytes, UINT size, UINT bind, ID3D11Buffer** output) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = bind;
    const D3D11_SUBRESOURCE_DATA data{bytes, 0, 0};
    return SUCCEEDED(device.CreateBuffer(&desc, &data, output));
}

} // namespace

bool UiPass::initialize(ID3D11Device* device) {
    if (m_device) { return true; }
    if (!device) { return false; }

    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = sizeof(UiDrawCb);
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    D3D11_BLEND_DESC blend{};
    auto& output = blend.RenderTarget[0];
    output.BlendEnable = TRUE;
    output.SrcBlend = output.SrcBlendAlpha = D3D11_BLEND_ONE;
    output.DestBlend = output.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    output.BlendOp = output.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    output.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.FrontFace = depth.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
        D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};

    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.DepthClipEnable = TRUE;
    auto scissor = rasterizer;
    scissor.ScissorEnable = TRUE;

    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;

    constexpr Rml::byte white[]{255, 255, 255, 255};
    m_white = createTexture(*device, {white, sizeof(white)}, {1, 1});
    if (!m_white ||
        FAILED(device->CreateBuffer(&constants, nullptr, m_constants.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateBlendState(&blend, m_blend.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&depth, m_depth.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateRasterizerState(&rasterizer, m_rasterizer.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateRasterizerState(&scissor, m_scissorRasterizer.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateSamplerState(&sampler, m_sampler.ReleaseAndGetAddressOf()))) {
        std::clog << "Game UI: failed to initialize the D3D11 overlay pass\n";
        return false;
    }
    D3D11_BUFFER_DESC gradient{};
    gradient.ByteWidth = sizeof(UiGradientCb);
    gradient.Usage = D3D11_USAGE_DEFAULT;
    gradient.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&gradient, nullptr, m_gradient.ReleaseAndGetAddressOf())) ||
        !m_effects.initialize(*device)) {
        std::clog << "Game UI: failed to initialize D3D11 effects\n";
        return false;
    }
    m_device = device;
    return true;
}

void UiPass::draw(ID3D11DeviceContext* context, ShaderLibrary& shaders, ID3D11RenderTargetView* target,
    int width, int height, GameUi& ui) {
    const auto shader = shaders.program(ShaderId::Ui);
    if (!m_device || !context || !shader.vertex || !target || width < 1 || height < 1) { return; }
    const ScopedUiState restore(*context);
    struct ResetContext {
        UiEffects& effects;
        ID3D11DeviceContext*& context;
        ShaderLibrary*& shaders;
        ~ResetContext() { effects.end(); context = nullptr; shaders = nullptr; }
    } reset{m_effects, m_context, m_shaders};
    m_context = context;
    m_shaders = &shaders;
    m_scissorEnabled = false;
    m_effects.begin(*context, *target, shaders.program(ShaderId::UiEffects), width, height);
    m_transform = Rml::Matrix4f::Identity();
    m_scaleX = 2.0f / static_cast<float>(width);
    m_scaleY = -2.0f / static_cast<float>(height);
    const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(m_blend.Get(), nullptr, 0xffffffff);
    context->OMSetDepthStencilState(m_depth.Get(), 0);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(m_rasterizer.Get());
    bindGeometryProgram(shader);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    ui.render();
}

void UiPass::bindGeometryProgram(ShaderProgram program, UINT vertexConstantSlot) {
    m_context->IASetInputLayout(program.layout);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(program.vertex, nullptr, 0);
    m_context->VSSetConstantBuffers(vertexConstantSlot, 1, m_constants.GetAddressOf());
    m_context->PSSetShader(program.pixel, nullptr, 0);
}

Rml::CompiledGeometryHandle UiPass::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices) {
    static_assert(sizeof(int) == sizeof(std::uint32_t));
    if (!m_device || vertices.empty() || indices.empty() ||
        vertices.size() > std::numeric_limits<UINT>::max() / sizeof(Rml::Vertex) ||
        indices.size() > std::numeric_limits<UINT>::max() / sizeof(int)) {
        return 0;
    }
    auto geometry = std::make_unique<Geometry>();
    if (!createBuffer(*m_device, vertices.data(), static_cast<UINT>(vertices.size() * sizeof(Rml::Vertex)),
            D3D11_BIND_VERTEX_BUFFER, geometry->vertices.GetAddressOf()) ||
        !createBuffer(*m_device, indices.data(), static_cast<UINT>(indices.size() * sizeof(int)),
            D3D11_BIND_INDEX_BUFFER, geometry->indices.GetAddressOf())) {
        std::clog << "Game UI: failed to upload D3D11 geometry\n";
        return 0;
    }
    geometry->indexCount = static_cast<UINT>(indices.size());
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
}

void UiPass::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) {
    if (!m_context || !handle) { return; }
    const auto& geometry = *reinterpret_cast<const Geometry*>(handle);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { return; }
    UiDrawCb constants{};
    std::memcpy(constants.transform, m_transform.data(), sizeof(constants.transform));
    constants.translation[0] = translation.x;
    constants.translation[1] = translation.y;
    constants.scale[0] = m_scaleX;
    constants.scale[1] = m_scaleY;
    std::memcpy(mapped.pData, &constants, sizeof(constants));
    m_context->Unmap(m_constants.Get(), 0);

    constexpr UINT stride = sizeof(Rml::Vertex);
    constexpr UINT offset = 0;
    auto* view = texture ? reinterpret_cast<ID3D11ShaderResourceView*>(texture) : m_white.Get();
    m_context->PSSetShaderResources(0, 1, &view);
    m_context->IASetVertexBuffers(0, 1, geometry.vertices.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(geometry.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->DrawIndexed(geometry.indexCount, 0, 0);
}

void UiPass::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    delete reinterpret_cast<Geometry*>(geometry);
}

Rml::TextureHandle UiPass::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) {
    std::string bytes;
    TextureData texture;
    std::string error;
    auto* files = Rml::GetFileInterface();
    if (!files || !files->LoadFile(source, bytes) || !decodeTexture(bytes, texture, error)) {
        std::clog << "Game UI: cannot load " << source << ' ' << error << '\n';
        return 0;
    }
    auto& pixels = texture.pixels;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned alpha = pixels[i + 3];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            pixels[i + channel] = static_cast<unsigned char>((pixels[i + channel] * alpha + 127) / 255);
        }
    }
    dimensions = {texture.width, texture.height};
    return GenerateTexture({pixels.data(), pixels.size()}, dimensions);
}

Rml::TextureHandle UiPass::GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i dimensions) {
    if (!m_device) { return 0; }
    return reinterpret_cast<Rml::TextureHandle>(createTexture(*m_device, pixels, dimensions).Detach());
}

void UiPass::ReleaseTexture(Rml::TextureHandle texture) {
    if (texture) { reinterpret_cast<ID3D11ShaderResourceView*>(texture)->Release(); }
}

void UiPass::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;
    m_effects.setScissor(enable, m_scissor);
    if (m_context) { m_context->RSSetState(enable ? m_scissorRasterizer.Get() : m_rasterizer.Get()); }
}

void UiPass::SetScissorRegion(Rml::Rectanglei region) {
    m_scissor = region;
    m_effects.setScissor(m_scissorEnabled, region);
    if (m_context) {
        const D3D11_RECT rectangle{region.Left(), region.Top(), region.Right(), region.Bottom()};
        m_context->RSSetScissorRects(1, &rectangle);
    }
}

void UiPass::SetTransform(const Rml::Matrix4f* transform) {
    m_transform = transform ? *transform : Rml::Matrix4f::Identity();
}

void UiPass::EnableClipMask(bool enable) { m_effects.enableClipMask(enable); }

void UiPass::RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) {
    if (!m_context) { return; }
    m_context->RSSetState(m_rasterizer.Get());
    m_effects.renderClipMask(*this, operation, geometry, translation);
    m_context->RSSetState(m_scissorEnabled ? m_scissorRasterizer.Get() : m_rasterizer.Get());
}

Rml::LayerHandle UiPass::PushLayer() { return m_effects.pushLayer(); }

void UiPass::CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend,
    Rml::Span<const Rml::CompiledFilterHandle> filters) {
    m_effects.composite(source, destination, blend, filters);
    if (m_context) {
        bindGeometryProgram(m_shaders->program(ShaderId::Ui));
        EnableScissorRegion(m_scissorEnabled);
        if (m_scissorEnabled) { SetScissorRegion(m_scissor); }
    }
}

void UiPass::PopLayer() { m_effects.popLayer(); }
Rml::TextureHandle UiPass::SaveLayerAsTexture() { return m_effects.saveTexture(); }
Rml::CompiledFilterHandle UiPass::SaveLayerAsMaskImage() { return m_effects.saveMask(); }

Rml::CompiledFilterHandle UiPass::CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) {
    return UiEffects::compileFilter(name, parameters);
}

void UiPass::ReleaseFilter(Rml::CompiledFilterHandle filter) { UiEffects::releaseFilter(filter); }

namespace {
struct UiShader {
    UiGradientCb gradient{};
    std::string path;
};
} // namespace

Rml::CompiledShaderHandle UiPass::CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) {
    auto shader = std::make_unique<UiShader>();
    auto& data = shader->gradient;
    Rml::Vector2f origin, direction;
    if (name == "shader") {
        shader->path = Rml::Get(parameters, "value", Rml::String{});
        if (!shader->path.starts_with("Assets/UI/") || !isShaderAssetPath(shader->path)) {
            std::clog << "Game UI: expected a project shader under Assets/UI/\n";
            return 0;
        }
    } else if (name == "linear-gradient") {
        origin = Rml::Get(parameters, "p0", Rml::Vector2f{});
        direction = Rml::Get(parameters, "p1", Rml::Vector2f{}) - origin;
    } else if (name == "radial-gradient") {
        data.gradientType = 1;
        origin = Rml::Get(parameters, "center", Rml::Vector2f{});
        const auto radius = Rml::Get(parameters, "radius", Rml::Vector2f{1, 1});
        direction = {1.0f / std::max(radius.x, 0.0001f), 1.0f / std::max(radius.y, 0.0001f)};
    } else if (name == "conic-gradient") {
        data.gradientType = 2;
        origin = Rml::Get(parameters, "center", Rml::Vector2f{});
        const auto angle = Rml::Get(parameters, "angle", 0.0f);
        direction = {std::cos(angle), std::sin(angle)};
    } else {
        return 0;
    }
    if (shader->path.empty()) {
        const auto stops = parameters.find("color_stop_list");
        if (stops == parameters.end() || stops->second.GetType() != Rml::Variant::COLORSTOPLIST) { return 0; }
        const auto& colors = stops->second.GetReference<Rml::ColorStopList>();
        if (colors.empty() || colors.size() > kUiGradientStops) {
            std::clog << "Game UI: gradient requires 1 to " << kUiGradientStops << " color stops\n";
            return 0;
        }
        data.origin[0] = origin.x;
        data.origin[1] = origin.y;
        data.direction[0] = direction.x;
        data.direction[1] = direction.y;
        data.repeating = Rml::Get(parameters, "repeating", false) ? 1u : 0u;
        data.stopCount = static_cast<unsigned>(colors.size());
        for (unsigned index = 0; index < data.stopCount; ++index) {
            const auto& stop = colors[index];
            data.positions[index] = stop.position.number;
            data.colors[index][0] = stop.color.red / 255.0f;
            data.colors[index][1] = stop.color.green / 255.0f;
            data.colors[index][2] = stop.color.blue / 255.0f;
            data.colors[index][3] = stop.color.alpha / 255.0f;
        }
    }
    return reinterpret_cast<Rml::CompiledShaderHandle>(shader.release());
}

void UiPass::RenderShader(Rml::CompiledShaderHandle handle, Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation, Rml::TextureHandle texture) {
    if (!m_context || !handle) { return; }
    const auto& shader = *reinterpret_cast<const UiShader*>(handle);
    const auto standard = m_shaders->program(ShaderId::Ui);
    if (shader.path.empty()) {
        bindGeometryProgram(m_shaders->program(ShaderId::UiGradient), 1);
        m_context->UpdateSubresource(m_gradient.Get(), 0, nullptr, &shader.gradient, 0, 0);
        m_context->PSSetConstantBuffers(0, 1, m_gradient.GetAddressOf());
    } else {
        auto* pixel = m_shaders->projectPixel(m_device, shader.path);
        if (!pixel) { return; }
        bindGeometryProgram({standard.vertex, pixel, standard.layout});
        ID3D11Buffer* empty = nullptr;
        m_context->PSSetConstantBuffers(0, 1, &empty);
    }
    RenderGeometry(geometry, translation, texture);
    bindGeometryProgram(standard);
}

void UiPass::ReleaseShader(Rml::CompiledShaderHandle shader) { delete reinterpret_cast<UiShader*>(shader); }

} // namespace sat
