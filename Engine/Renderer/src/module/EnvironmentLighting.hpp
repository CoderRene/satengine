#pragma once

#include "ShaderLibrary.hpp"

#include <array>
#include <string_view>

namespace sat {

struct EnvironmentLightingViews {
    ID3D11ShaderResourceView* diffuse = nullptr;
    ID3D11ShaderResourceView* specular = nullptr;
    ID3D11ShaderResourceView* brdf = nullptr;
    float maxSpecularLod = 0.0f;
};

// Render thread only. Returned views are borrowed until the next prepare or reset.
class EnvironmentLighting {
public:
    [[nodiscard]] EnvironmentLightingViews prepare(ID3D11Device& device, ID3D11DeviceContext& context,
        const ShaderLibrary& shaders, std::string_view path, ID3D11ShaderResourceView* panorama,
        ID3D11SamplerState* panoramaSampler, ID3D11SamplerState* linearClampSampler);
    void resetEnvironments();

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source, diffuse, specular;
    };

    std::array<Entry, 2> m_entries;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_cubeShader, m_filterShader, m_brdfShader;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_brdf;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    bool m_brdfAttempted = false;
};

} // namespace sat
