#include "UiEffects.hpp"

#include "ShaderLayout.hpp"
#include "UiPass.hpp"

#include <RmlUi/Core/Dictionary.h>
#include <RmlUi/Core/Variant.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace sat {
namespace {

using Microsoft::WRL::ComPtr;

enum class Effect : unsigned { Copy, Color, Opacity, Blur, Shadow, Mask };

struct Surface {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11ShaderResourceView> view;
};

struct Filter {
    Effect effect = Effect::Copy;
    Rml::Matrix4f matrix = Rml::Matrix4f::Identity();
    Rml::Vector2f offset{};
    Rml::Vector4f color{};
    float value = 1.0f;
    ComPtr<ID3D11ShaderResourceView> mask;
};

bool createSurface(ID3D11Device& device, int width, int height, Surface& surface) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    Surface candidate;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, candidate.texture.GetAddressOf())) ||
        FAILED(device.CreateRenderTargetView(candidate.texture.Get(), nullptr, candidate.target.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(candidate.texture.Get(), nullptr, candidate.view.GetAddressOf()))) {
        std::clog << "Game UI: cannot allocate effect surface\n";
        return false;
    }
    surface = std::move(candidate);
    return true;
}

} // namespace

struct UiEffects::State {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* base = nullptr;
    ShaderProgram shader{};
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> rasterizer;
    ComPtr<ID3D11DepthStencilView> stencil;
    ComPtr<ID3D11DepthStencilState> stencilTest, stencilReplace, stencilIntersect, depthDisabled;
    ComPtr<ID3D11BlendState> blend, writeMask;
    std::vector<Surface> layers;
    std::array<Surface, 3> scratch;
    std::size_t depth = 0;
    int width = 0;
    int height = 0;
    bool scissor = false;
    Rml::Rectanglei region = Rml::Rectanglei::MakeInvalid();
    bool clipping = false;
    UINT reference = 0;

    ID3D11RenderTargetView* target(std::size_t layer) const {
        return layer ? layers[layer - 1].target.Get() : base;
    }

    void bind(std::size_t layer) {
        auto* output = target(layer);
        context->OMSetRenderTargets(1, &output, stencil.Get());
    }

    void clipState(bool enabled) {
        context->OMSetDepthStencilState(enabled ? stencilTest.Get() : depthDisabled.Get(), reference);
    }

    D3D11_RECT bounds() const {
        if (!scissor) { return {0, 0, width, height}; }
        return {std::clamp(region.Left(), 0, width), std::clamp(region.Top(), 0, height),
            std::clamp(region.Right(), 0, width), std::clamp(region.Bottom(), 0, height)};
    }

    bool ensureScratch() {
        for (auto& surface : scratch) {
            if (!surface.target && !createSurface(*device, width, height, surface)) { return false; }
        }
        return true;
    }

    void draw(ID3D11ShaderResourceView* input, ID3D11ShaderResourceView* secondary,
        ID3D11RenderTargetView* output, const Filter& filter, bool useBlend, bool final, Rml::Vector2f step = {}) {
        UiEffectCb data{};
        std::copy_n(filter.matrix.data(), 16, data.matrix);
        data.color[0] = filter.color.x;
        data.color[1] = filter.color.y;
        data.color[2] = filter.color.z;
        data.color[3] = filter.color.w;
        data.offset[0] = filter.offset.x / static_cast<float>(width);
        data.offset[1] = filter.offset.y / static_cast<float>(height);
        data.step[0] = step.x;
        data.step[1] = step.y;
        data.value = filter.value;
        data.effect = static_cast<unsigned>(filter.effect);
        context->UpdateSubresource(constants.Get(), 0, nullptr, &data, 0, 0);
        context->OMSetRenderTargets(1, &output, final ? stencil.Get() : nullptr);
        context->OMSetBlendState(useBlend ? blend.Get() : nullptr, nullptr, 0xffffffff);
        clipState(final && clipping);
        const D3D11_RECT rect = final ? bounds() : D3D11_RECT{0, 0, width, height};
        context->RSSetScissorRects(1, &rect);
        ID3D11ShaderResourceView* inputs[]{input, secondary};
        context->PSSetShaderResources(0, 2, inputs);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty[2]{};
        context->PSSetShaderResources(0, 2, empty);
    }

    void blur(ID3D11ShaderResourceView* input, Surface& temporary, Surface& output, float sigma) {
        Filter blurFilter;
        blurFilter.effect = Effect::Blur;
        blurFilter.value = sigma;
        draw(input, nullptr, temporary.target.Get(), blurFilter, false, false, {1.0f / width, 0});
        draw(temporary.view.Get(), nullptr, output.target.Get(), blurFilter, false, false, {0, 1.0f / height});
    }
};

UiEffects::UiEffects() : m_state(std::make_unique<State>()) {}
UiEffects::~UiEffects() = default;

bool UiEffects::initialize(ID3D11Device& device) {
    auto& s = *m_state;
    if (s.device) { return true; }
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(UiEffectCb);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.ScissorEnable = TRUE;
    raster.DepthClipEnable = TRUE;

    D3D11_DEPTH_STENCIL_DESC stencil{};
    stencil.DepthFunc = D3D11_COMPARISON_ALWAYS;
    stencil.StencilEnable = TRUE;
    stencil.StencilReadMask = stencil.StencilWriteMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    const auto createStencil = [&](D3D11_COMPARISON_FUNC comparison, D3D11_STENCIL_OP operation,
        ID3D11DepthStencilState** output) {
        stencil.FrontFace = stencil.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, operation, comparison};
        return SUCCEEDED(device.CreateDepthStencilState(&stencil, output));
    };

    D3D11_BLEND_DESC blend{};
    auto& color = blend.RenderTarget[0];
    color.BlendEnable = TRUE;
    color.SrcBlend = color.SrcBlendAlpha = D3D11_BLEND_ONE;
    color.DestBlend = color.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    color.BlendOp = color.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    color.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device.CreateBlendState(&blend, s.blend.ReleaseAndGetAddressOf()))) { return false; }
    color.BlendEnable = FALSE;
    color.RenderTargetWriteMask = 0;
    if (FAILED(device.CreateBlendState(&blend, s.writeMask.ReleaseAndGetAddressOf())) ||
        FAILED(device.CreateBuffer(&buffer, nullptr, s.constants.ReleaseAndGetAddressOf())) ||
        FAILED(device.CreateRasterizerState(&raster, s.rasterizer.ReleaseAndGetAddressOf())) ||
        !createStencil(D3D11_COMPARISON_EQUAL, D3D11_STENCIL_OP_KEEP, s.stencilTest.ReleaseAndGetAddressOf()) ||
        !createStencil(D3D11_COMPARISON_ALWAYS, D3D11_STENCIL_OP_REPLACE, s.stencilReplace.ReleaseAndGetAddressOf()) ||
        !createStencil(D3D11_COMPARISON_EQUAL, D3D11_STENCIL_OP_INCR_SAT, s.stencilIntersect.ReleaseAndGetAddressOf())) {
        return false;
    }
    stencil.StencilEnable = FALSE;
    if (FAILED(device.CreateDepthStencilState(&stencil, s.depthDisabled.ReleaseAndGetAddressOf()))) { return false; }
    s.device = &device;
    return true;
}

void UiEffects::begin(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
    ShaderProgram program, int width, int height) {
    auto& s = *m_state;
    if (s.width != width || s.height != height) {
        s.width = width;
        s.height = height;
        s.layers.clear();
        s.scratch = {};
        s.stencil.Reset();
    }
    s.context = &context;
    s.base = &target;
    s.shader = program;
    s.depth = 0;
    s.scissor = s.clipping = false;
    s.reference = 0;
}

void UiEffects::end() {
    m_state->context = nullptr;
    m_state->base = nullptr;
}

void UiEffects::setScissor(bool enabled, Rml::Rectanglei region) {
    m_state->scissor = enabled;
    m_state->region = region;
}

void UiEffects::enableClipMask(bool enabled) {
    auto& s = *m_state;
    if (!s.context) { return; }
    if (enabled && !s.stencil) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(s.width);
        desc.Height = static_cast<UINT>(s.height);
        desc.MipLevels = desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(s.device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
            FAILED(s.device->CreateDepthStencilView(texture.Get(), nullptr, s.stencil.GetAddressOf()))) {
            std::clog << "Game UI: cannot allocate clip stencil\n";
            return;
        }
        s.context->ClearDepthStencilView(s.stencil.Get(), D3D11_CLEAR_STENCIL, 1.0f, 0);
        s.bind(s.depth);
    }
    s.clipping = enabled;
    s.clipState(enabled);
}

void UiEffects::renderClipMask(UiPass& pass, Rml::ClipMaskOperation operation,
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) {
    auto& s = *m_state;
    if (!s.context || !s.stencil) { return; }
    const bool intersect = operation == Rml::ClipMaskOperation::Intersect;
    if (!intersect) { s.context->ClearDepthStencilView(s.stencil.Get(), D3D11_CLEAR_STENCIL, 1.0f, 0); }
    s.context->OMSetDepthStencilState(intersect ? s.stencilIntersect.Get() : s.stencilReplace.Get(),
        intersect ? s.reference : 1u);
    s.context->OMSetBlendState(s.writeMask.Get(), nullptr, 0xffffffff);
    pass.RenderGeometry(geometry, translation, {});
    s.context->OMSetBlendState(s.blend.Get(), nullptr, 0xffffffff);
    s.reference = intersect ? std::min(s.reference + 1u, 255u) :
        operation == Rml::ClipMaskOperation::Set ? 1u : 0u;
    s.clipState(s.clipping);
}

Rml::LayerHandle UiEffects::pushLayer() {
    auto& s = *m_state;
    if (!s.context) { return 0; }
    ++s.depth;
    if (s.layers.size() < s.depth) { s.layers.resize(s.depth); }
    auto& layer = s.layers[s.depth - 1];
    if (!layer.target) { (void)createSurface(*s.device, s.width, s.height, layer); }
    s.bind(s.depth);
    if (layer.target) {
        constexpr float kTransparent[4]{};
        s.context->ClearRenderTargetView(layer.target.Get(), kTransparent);
    }
    return s.depth;
}

void UiEffects::popLayer() {
    auto& s = *m_state;
    if (s.context && s.depth) { s.bind(--s.depth); }
}

void UiEffects::composite(Rml::LayerHandle source, Rml::LayerHandle destination,
    Rml::BlendMode blend, Rml::Span<const Rml::CompiledFilterHandle> filters) {
    auto& s = *m_state;
    if (!s.context || !s.shader.vertex || !s.target(source) || !s.target(destination)) { return; }
    const auto clipped = s.bounds();
    if (clipped.left >= clipped.right || clipped.top >= clipped.bottom) { return; }
    const bool needsSnapshot = source == 0 || source == destination;
    if ((needsSnapshot || !filters.empty()) && !s.ensureScratch()) { return; }
    ID3D11ShaderResourceView* image = source ? s.layers[source - 1].view.Get() : nullptr;
    std::size_t next = 0;
    if (needsSnapshot) {
        ComPtr<ID3D11Resource> resource;
        s.target(source)->GetResource(resource.GetAddressOf());
        s.context->OMSetRenderTargets(0, nullptr, nullptr);
        s.context->CopyResource(s.scratch[0].texture.Get(), resource.Get());
        image = s.scratch[0].view.Get();
        next = 1;
    }

    s.context->VSSetShader(s.shader.vertex, nullptr, 0);
    s.context->PSSetShader(s.shader.pixel, nullptr, 0);
    s.context->PSSetConstantBuffers(0, 1, s.constants.GetAddressOf());
    s.context->IASetInputLayout(nullptr);
    s.context->RSSetState(s.rasterizer.Get());
    for (const auto handle : filters) {
        const auto& filter = *reinterpret_cast<const Filter*>(handle);
        auto& output = s.scratch[next];
        auto& temporary = s.scratch[2];
        if (filter.effect == Effect::Blur && filter.value >= 0.5f) {
            s.blur(image, temporary, output, filter.value);
        } else if (filter.effect == Effect::Shadow && filter.value >= 0.5f) {
            // Blur the source alpha, then composite the tinted, offset shadow behind the source.
            Filter vertical;
            vertical.effect = Effect::Blur;
            vertical.value = filter.value;
            s.draw(image, nullptr, temporary.target.Get(), vertical, false, false, {1.0f / s.width, 0});
            s.draw(image, temporary.view.Get(), output.target.Get(), filter, false, false, {0, 1.0f / s.height});
        } else {
            const Effect effect = filter.effect;
            s.draw(image, effect == Effect::Mask ? filter.mask.Get() : image, output.target.Get(),
                effect == Effect::Blur ? Filter{} : filter, false, false);
        }
        image = output.view.Get();
        next = 1 - next;
    }
    s.draw(image, nullptr, s.target(destination), Filter{}, blend == Rml::BlendMode::Blend, true);
    s.bind(s.depth);
    s.clipState(s.clipping);
    s.context->OMSetBlendState(s.blend.Get(), nullptr, 0xffffffff);
}

Rml::TextureHandle UiEffects::saveTexture() {
    auto& s = *m_state;
    if (!s.context || !s.target(s.depth)) { return 0; }
    const auto bounds = s.bounds();
    const int width = static_cast<int>(bounds.right - bounds.left);
    const int height = static_cast<int>(bounds.bottom - bounds.top);
    Surface snapshot;
    if (width <= 0 || height <= 0 || !createSurface(*s.device, width, height, snapshot)) { return 0; }
    ComPtr<ID3D11Resource> source;
    s.target(s.depth)->GetResource(source.GetAddressOf());
    const D3D11_BOX box{static_cast<UINT>(bounds.left), static_cast<UINT>(bounds.top), 0,
        static_cast<UINT>(bounds.right), static_cast<UINT>(bounds.bottom), 1};
    s.context->OMSetRenderTargets(0, nullptr, nullptr);
    s.context->CopySubresourceRegion(snapshot.texture.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
    s.bind(s.depth);
    return reinterpret_cast<Rml::TextureHandle>(snapshot.view.Detach());
}

Rml::CompiledFilterHandle UiEffects::saveMask() {
    auto& s = *m_state;
    if (!s.context || !s.depth || !s.layers[s.depth - 1].texture) { return 0; }
    Surface snapshot;
    if (!createSurface(*s.device, s.width, s.height, snapshot)) { return 0; }
    s.context->OMSetRenderTargets(0, nullptr, nullptr);
    s.context->CopyResource(snapshot.texture.Get(), s.layers[s.depth - 1].texture.Get());
    s.bind(s.depth);
    auto filter = std::make_unique<Filter>();
    filter->effect = Effect::Mask;
    filter->mask = std::move(snapshot.view);
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter.release());
}

Rml::CompiledFilterHandle UiEffects::compileFilter(const Rml::String& name, const Rml::Dictionary& parameters) {
    auto filter = std::make_unique<Filter>();
    using Rml::Matrix4f;
    const float value = Rml::Get(parameters, "value", 1.0f);
    if (name == "opacity") {
        filter->effect = Effect::Opacity;
        filter->value = std::clamp(value, 0.0f, 1.0f);
    } else if (name == "blur" || name == "drop-shadow") {
        filter->effect = name == "blur" ? Effect::Blur : Effect::Shadow;
        filter->value = std::clamp(Rml::Get(parameters, "sigma", 0.0f), 0.0f, 64.0f);
        filter->offset = Rml::Get(parameters, "offset", Rml::Vector2f{});
        const auto color = Rml::Get(parameters, "color", Rml::Colourb{}).ToPremultiplied();
        filter->color = {color.red / 255.0f, color.green / 255.0f, color.blue / 255.0f, color.alpha / 255.0f};
    } else {
        filter->effect = Effect::Color;
        auto& matrix = filter->matrix;
        if (name == "brightness") {
            matrix = Matrix4f::Diag(value, value, value, 1);
        } else if (name == "contrast" || name == "invert") {
            const float factor = name == "contrast" ? value : 1 - 2 * std::clamp(value, 0.0f, 1.0f);
            const float bias = (1 - factor) * 0.5f;
            matrix = Matrix4f::Diag(factor, factor, factor, 1);
            matrix.SetColumn(3, {bias, bias, bias, 1});
        } else if (name == "grayscale" || name == "saturate" || name == "sepia") {
            const auto target = name == "sepia"
                ? Matrix4f::FromRows({.393f, .769f, .189f, 0}, {.349f, .686f, .168f, 0},
                    {.272f, .534f, .131f, 0}, {0, 0, 0, 1})
                : Matrix4f::FromRows({.2126f, .7152f, .0722f, 0}, {.2126f, .7152f, .0722f, 0},
                    {.2126f, .7152f, .0722f, 0}, {0, 0, 0, 1});
            const float weight = name == "saturate" ? 1 - value : std::clamp(value, 0.0f, 1.0f);
            matrix = Matrix4f::Identity() * (1 - weight) + target * weight;
        } else if (name == "hue-rotate") {
            const float c = std::cos(value), s = std::sin(value);
            matrix = Matrix4f::FromRows(
                {.213f + .787f*c - .213f*s, .715f - .715f*c - .715f*s, .072f - .072f*c + .928f*s, 0},
                {.213f - .213f*c + .143f*s, .715f + .285f*c + .140f*s, .072f - .072f*c - .283f*s, 0},
                {.213f - .213f*c - .787f*s, .715f - .715f*c + .715f*s, .072f + .928f*c + .072f*s, 0},
                {0, 0, 0, 1});
        } else {
            std::clog << "Game UI: unsupported filter " << name << '\n';
            return 0;
        }
    }
    return reinterpret_cast<Rml::CompiledFilterHandle>(filter.release());
}

void UiEffects::releaseFilter(Rml::CompiledFilterHandle filter) {
    delete reinterpret_cast<Filter*>(filter);
}

} // namespace sat
