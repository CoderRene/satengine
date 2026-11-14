#pragma once

#include "ShaderLibrary.hpp"

#include <RmlUi/Core/RenderInterface.h>

#include <memory>

namespace sat {

class UiPass;

class UiEffects {
public:
    UiEffects();
    ~UiEffects();
    [[nodiscard]] bool initialize(ID3D11Device& device);
    void begin(ID3D11DeviceContext& context, ID3D11RenderTargetView& target,
        ShaderProgram program, int width, int height);
    void end();
    void setScissor(bool enabled, Rml::Rectanglei region);
    void enableClipMask(bool enabled);
    void renderClipMask(UiPass& pass, Rml::ClipMaskOperation operation,
        Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation);
    Rml::LayerHandle pushLayer();
    void popLayer();
    void composite(Rml::LayerHandle source, Rml::LayerHandle destination,
        Rml::BlendMode blend, Rml::Span<const Rml::CompiledFilterHandle> filters);
    Rml::TextureHandle saveTexture();
    Rml::CompiledFilterHandle saveMask();
    static Rml::CompiledFilterHandle compileFilter(const Rml::String& name, const Rml::Dictionary& parameters);
    static void releaseFilter(Rml::CompiledFilterHandle filter);

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace sat
