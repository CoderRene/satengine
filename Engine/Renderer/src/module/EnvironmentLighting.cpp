#include "EnvironmentLighting.hpp"

#include <array>
#include <iostream>
#include <utility>

namespace sat {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kCubeSize = 128;
constexpr UINT kSourceMips = 8;
constexpr UINT kSpecularMips = 6;
constexpr UINT kDiffuseSize = 32;
constexpr UINT kBrdfSize = 128;
constexpr UINT kFaceCount = 6;
constexpr UINT kGroupSize = 8;

struct BakeConstants {
    UINT size;
    UINT diffuse;
    float roughness;
    float sourceTexelSolidAngle;
};
static_assert(sizeof(BakeConstants) == 16);

bool createCube(ID3D11Device& device, UINT size, UINT mips, bool generateMips, DXGI_FORMAT format,
    ID3D11Texture2D** texture, ID3D11ShaderResourceView** view) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = size;
    desc.MipLevels = mips;
    desc.ArraySize = kFaceCount;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    if (generateMips) {
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = desc.Format;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srv.TextureCube.MipLevels = mips;
    return SUCCEEDED(device.CreateTexture2D(&desc, nullptr, texture)) &&
        SUCCEEDED(device.CreateShaderResourceView(*texture, &srv, view));
}

bool createCubeOutput(ID3D11Device& device, ID3D11Texture2D* texture, UINT mip, DXGI_FORMAT format, ID3D11UnorderedAccessView** output) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
    desc.Texture2DArray.MipSlice = mip;
    desc.Texture2DArray.ArraySize = kFaceCount;
    return SUCCEEDED(device.CreateUnorderedAccessView(texture, &desc, output));
}

void dispatch(ID3D11DeviceContext& context, ID3D11UnorderedAccessView* output, UINT size, UINT slices) {
    ID3D11UnorderedAccessView* nullOutput = nullptr;
    const UINT groups = (size + kGroupSize - 1) / kGroupSize;
    context.CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    context.Dispatch(groups, groups, slices);
    context.CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
}

ComPtr<ID3D11ShaderResourceView> bakeBrdf(ID3D11Device& device, ID3D11DeviceContext& context,
    ID3D11ComputeShader* shader) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = kBrdfSize;
    desc.ArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11UnorderedAccessView> output;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf())) ||
        FAILED(device.CreateUnorderedAccessView(texture.Get(), nullptr, output.GetAddressOf()))) {
        return {};
    }
    context.CSSetShader(shader, nullptr, 0);
    dispatch(context, output.Get(), kBrdfSize, 1);
    context.CSSetShader(nullptr, nullptr, 0);
    return view;
}

bool bakeEnvironment(ID3D11Device& device, ID3D11DeviceContext& context, ID3D11Buffer* constants,
    ID3D11ComputeShader* cubeShader, ID3D11ComputeShader* filterShader, ID3D11ShaderResourceView* panorama,
    ID3D11SamplerState* panoramaSampler, ID3D11SamplerState* linearClampSampler,
    ComPtr<ID3D11ShaderResourceView>& diffuse, ComPtr<ID3D11ShaderResourceView>& specular) {
    ComPtr<ID3D11Texture2D> sourceTexture, diffuseTexture, specularTexture;
    ComPtr<ID3D11ShaderResourceView> source, diffuseCandidate, specularCandidate;
    ComPtr<ID3D11UnorderedAccessView> sourceOutput, diffuseOutput;
    std::array<ComPtr<ID3D11UnorderedAccessView>, kSpecularMips - 1> specularOutputs;
    D3D11_SHADER_RESOURCE_VIEW_DESC panoramaDesc{};
    panorama->GetDesc(&panoramaDesc);
    const DXGI_FORMAT format = panoramaDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ?
        DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!createCube(device, kCubeSize, kSourceMips, true, format, sourceTexture.GetAddressOf(), source.GetAddressOf()) ||
        !createCube(device, kDiffuseSize, 1, false, format, diffuseTexture.GetAddressOf(), diffuseCandidate.GetAddressOf()) ||
        !createCube(device, kCubeSize, kSpecularMips, false, format, specularTexture.GetAddressOf(), specularCandidate.GetAddressOf()) ||
        !createCubeOutput(device, sourceTexture.Get(), 0, format, sourceOutput.GetAddressOf()) ||
        !createCubeOutput(device, diffuseTexture.Get(), 0, format, diffuseOutput.GetAddressOf())) {
        return false;
    }
    for (UINT mip = 1; mip < kSpecularMips; ++mip) {
        if (!createCubeOutput(device, specularTexture.Get(), mip, format, specularOutputs[mip - 1].GetAddressOf())) {
            return false;
        }
    }

    const auto bake = [&](ID3D11UnorderedAccessView* output, UINT size, UINT isDiffuse, float roughness) {
        constexpr float kSourceTexelSolidAngle = 12.56637061435917f / (6.0f * kCubeSize * kCubeSize);
        const BakeConstants data{size, isDiffuse, roughness, kSourceTexelSolidAngle};
        context.UpdateSubresource(constants, 0, nullptr, &data, 0, 0);
        dispatch(context, output, size, kFaceCount);
    };

    context.CSSetConstantBuffers(0, 1, &constants);
    context.CSSetShader(cubeShader, nullptr, 0);
    context.CSSetShaderResources(0, 1, &panorama);
    context.CSSetSamplers(0, 1, &panoramaSampler);
    bake(sourceOutput.Get(), kCubeSize, 0, 0.0f);
    context.GenerateMips(source.Get());
    for (UINT face = 0; face < kFaceCount; ++face) {
        context.CopySubresourceRegion(specularTexture.Get(), D3D11CalcSubresource(0, face, kSpecularMips), 0, 0, 0,
            sourceTexture.Get(), D3D11CalcSubresource(0, face, kSourceMips), nullptr);
    }

    context.CSSetShader(filterShader, nullptr, 0);
    context.CSSetShaderResources(0, 1, source.GetAddressOf());
    context.CSSetSamplers(0, 1, &linearClampSampler);
    bake(diffuseOutput.Get(), kDiffuseSize, 1, 0.0f);
    for (UINT mip = 1; mip < kSpecularMips; ++mip) {
        bake(specularOutputs[mip - 1].Get(), kCubeSize >> mip, 0,
            static_cast<float>(mip) / static_cast<float>(kSpecularMips - 1));
    }

    ID3D11ShaderResourceView* nullInput = nullptr;
    ID3D11SamplerState* nullSampler = nullptr;
    ID3D11Buffer* nullBuffer = nullptr;
    context.CSSetShaderResources(0, 1, &nullInput);
    context.CSSetSamplers(0, 1, &nullSampler);
    context.CSSetConstantBuffers(0, 1, &nullBuffer);
    context.CSSetShader(nullptr, nullptr, 0);
    diffuse = std::move(diffuseCandidate);
    specular = std::move(specularCandidate);
    return true;
}

} // namespace

void EnvironmentLighting::resetEnvironments() {
    m_entries = {};
}

EnvironmentLightingViews EnvironmentLighting::prepare(ID3D11Device& device, ID3D11DeviceContext& context,
    const ShaderLibrary& shaders, std::string_view path, ID3D11ShaderResourceView* panorama,
    ID3D11SamplerState* panoramaSampler, ID3D11SamplerState* linearClampSampler) {
    if (!panorama || !panoramaSampler || !linearClampSampler) { return {}; }
    auto* cubeShader = shaders.compute(ComputeShaderId::EnvironmentCube);
    auto* filterShader = shaders.compute(ComputeShaderId::EnvironmentFilter);
    auto* brdfShader = shaders.compute(ComputeShaderId::EnvironmentBrdf);
    if (!cubeShader || !filterShader || !brdfShader) { return {}; }
    if (m_cubeShader.Get() != cubeShader || m_filterShader.Get() != filterShader) {
        resetEnvironments();
        m_cubeShader = cubeShader;
        m_filterShader = filterShader;
    }
    if (m_brdfShader.Get() != brdfShader) {
        m_brdfShader = brdfShader;
        m_brdf.Reset();
        m_brdfAttempted = false;
    }
    if (!m_brdfAttempted) {
        m_brdfAttempted = true;
        m_brdf = bakeBrdf(device, context, brdfShader);
        if (!m_brdf) { std::cerr << "Cannot bake environment BRDF lookup\n"; }
    }
    if (!m_brdf) { return {}; }

    auto& mostRecent = m_entries[0];
    if (mostRecent.source.Get() != panorama) {
        auto& leastRecent = m_entries[1];
        if (leastRecent.source.Get() != panorama) {
            leastRecent = {};
            // Keep the source alive so a replacement cannot reuse its identity.
            leastRecent.source = panorama;
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(BakeConstants);
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if ((!m_constants && FAILED(device.CreateBuffer(&desc, nullptr, m_constants.GetAddressOf()))) ||
                !bakeEnvironment(device, context, m_constants.Get(), cubeShader, filterShader, panorama,
                    panoramaSampler, linearClampSampler, leastRecent.diffuse, leastRecent.specular)) {
                std::cerr << path << ": cannot bake environment cubemaps\n";
            }
        }
        std::swap(mostRecent, leastRecent);
    }
    if (!mostRecent.diffuse) { return {}; }
    return {mostRecent.diffuse.Get(), mostRecent.specular.Get(), m_brdf.Get(), static_cast<float>(kSpecularMips - 1)};
}

} // namespace sat
