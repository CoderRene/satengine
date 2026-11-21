#include "module/EnvironmentLighting.hpp"
#include "module/MeshPass.hpp"

#include "AssetData.hpp"
#include "AssetLibrary.hpp"

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> testPanorama(ID3D11Device& device, bool directional) {
    constexpr UINT kWidth = 128, kHeight = 64;
    std::array<unsigned char, kWidth * kHeight * 4> pixels{};
    for (UINT y = 0; y < kHeight; ++y) {
        for (UINT x = 0; x < kWidth; ++x) {
            // environmentUv points +Z at the center of the panorama.
            const bool bright = x >= 61 && x < 67 && y >= 29 && y < 35;
            const auto value = static_cast<unsigned char>(directional ? (bright ? 255 : 0) : 128);
            const auto index = (y * kWidth + x) * 4;
            pixels[index] = pixels[index + 1] = pixels[index + 2] = value;
            pixels[index + 3] = 255;
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), kWidth * 4, 0};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    if (SUCCEEDED(device.CreateTexture2D(&desc, &data, texture.GetAddressOf())) &&
        SUCCEEDED(device.CreateShaderResourceView(texture.Get(), nullptr, view.GetAddressOf()))) {
        return view;
    }
    return {};
}

std::array<float, 2> readEnvironmentTexel(ID3D11Device& device, ID3D11DeviceContext& context,
    ID3D11ShaderResourceView& view, UINT face, UINT mip, UINT x, UINT y) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Resource> resource;
    ComPtr<ID3D11Texture2D> texture;
    view.GetResource(resource.GetAddressOf());
    if (FAILED(resource.As(&texture))) { return {-1.0f, -1.0f}; }
    D3D11_TEXTURE2D_DESC source{};
    texture->GetDesc(&source);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = source.Format;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, readback.GetAddressOf()))) { return {-1.0f, -1.0f}; }
    const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
    context.CopySubresourceRegion(readback.Get(), 0, 0, 0, 0, texture.Get(),
        D3D11CalcSubresource(mip, face, source.MipLevels), &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { return {-1.0f, -1.0f}; }
    std::uint16_t pixel[2];
    std::memcpy(pixel, mapped.pData, sizeof(pixel));
    context.Unmap(readback.Get(), 0);
    const auto decode = [](std::uint16_t value) {
        const int exponent = (value >> 10) & 31;
        if (exponent == 31 || (value & 0x8000) != 0) { return -1.0f; }
        return std::ldexp(float((value & 1023) + (exponent == 0 ? 0 : 1024)), std::max(1, exponent) - 25);
    };
    return {decode(pixel[0]), decode(pixel[1])};
}

std::array<double, 2> referenceEnvironmentBrdf(UINT x, UINT y) {
    // Integrate the BRDF with a dense grid over GGX-distributed half vectors.
    constexpr int kSteps = 128;
    const double nv = (x + 0.5) / 128.0;
    const double roughness = (y + 0.5) / 128.0;
    const double alpha = roughness * roughness;
    const double k = 0.5 * alpha;
    const double vx = std::sqrt(1.0 - nv * nv);
    std::array<double, 2> result{};
    for (int i = 0; i < kSteps; ++i) {
        const double u = (i + 0.5) / kSteps;
        const double nh = std::sqrt((1.0 - u) / (1.0 - u + alpha * alpha * u));
        const double sinTheta = std::sqrt(1.0 - nh * nh);
        for (int j = 0; j < kSteps; ++j) {
            const double phi = (j + 0.5) * (2.0 * std::numbers::pi / kSteps);
            const double vh = std::max(0.0, vx * std::cos(phi) * sinTheta + nv * nh);
            const double nl = 2.0 * vh * nh - nv;
            if (nl <= 0.0) { continue; }
            const double smith = nv / std::lerp(nv, 1.0, k) * nl / std::lerp(nl, 1.0, k);
            const double visibility = smith * vh / (nh * nv);
            const double fresnel = std::pow(1.0 - vh, 5.0);
            result[0] += visibility * (1.0 - fresnel);
            result[1] += visibility * fresnel;
        }
    }
    return {result[0] / (kSteps * kSteps), result[1] / (kSteps * kSteps)};
}

bool checkEnvironmentLighting(ID3D11Device& device, ID3D11DeviceContext& context, const sat::ShaderLibrary& shaders) {
    using Microsoft::WRL::ComPtr;
    auto constant = testPanorama(device, false);
    auto directional = testPanorama(device, true);
    auto replacement = testPanorama(device, true);
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxAnisotropy = 1;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> panoramaSampler, clampSampler;
    if (!constant || !directional || !replacement ||
        FAILED(device.CreateSamplerState(&desc, panoramaSampler.GetAddressOf()))) { return false; }
    desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device.CreateSamplerState(&desc, clampSampler.GetAddressOf()))) { return false; }

    sat::EnvironmentLighting cache;
    const auto bake = [&](ID3D11ShaderResourceView* source) {
        return cache.prepare(device, context, shaders, "Test environment", source, panoramaSampler.Get(), clampSampler.Get());
    };
    const auto solid = bake(constant.Get());
    if (!solid.diffuse || !solid.specular || !solid.brdf || solid.maxSpecularLod != 5.0f) { return false; }
    const auto read = [&](ID3D11ShaderResourceView* view, UINT face, UINT mip, UINT x, UINT y) {
        return readEnvironmentTexel(device, context, *view, face, mip, x, y);
    };
    constexpr float kLinearGray = 0.21586f;
    if (std::abs(read(solid.diffuse, 4, 0, 16, 16)[0] - kLinearGray) > 0.003f ||
        std::abs(read(solid.specular, 5, 5, 2, 2)[0] - kLinearGray) > 0.003f) { return false; }
    constexpr UINT kLutSamples[][2] = {{16, 32}, {63, 64}, {116, 12}, {90, 116}};
    for (const auto& point : kLutSamples) {
        const auto actual = read(solid.brdf, 0, 0, point[0], point[1]);
        const auto expected = referenceEnvironmentBrdf(point[0], point[1]);
        if (std::abs(actual[0] - expected[0]) > 0.025 || std::abs(actual[1] - expected[1]) > 0.015) {
            return false;
        }
    }

    const auto spot = bake(directional.Get());
    if (!spot.diffuse || !spot.specular || spot.brdf != solid.brdf) { return false; }
    const float sharpCenter = read(spot.specular, 4, 0, 64, 64)[0];
    const float sharpAside = read(spot.specular, 4, 0, 88, 64)[0];
    const float broadCenter = read(spot.specular, 4, 3, 8, 8)[0];
    const float broadAside = read(spot.specular, 4, 3, 11, 8)[0];
    const float frontDiffuse = read(spot.diffuse, 4, 0, 16, 16)[0];
    const float backDiffuse = read(spot.diffuse, 5, 0, 16, 16)[0];
    if (sharpCenter < 0.9f || sharpAside != 0.0f || broadCenter <= broadAside ||
        broadCenter > 0.7f || broadAside < 0.005f || frontDiffuse < 0.01f || backDiffuse != 0.0f) {
        return false;
    }

    const ComPtr<ID3D11ShaderResourceView> oldSpecular(spot.specular);
    const auto reused = bake(constant.Get());
    if (reused.diffuse != solid.diffuse || reused.specular != solid.specular || bake(nullptr).brdf) { return false; }
    const auto replaced = bake(replacement.Get());
    if (!replaced.specular || replaced.specular == oldSpecular.Get() || replaced.brdf != solid.brdf ||
        bake(constant.Get()).specular != solid.specular) { return false; }
    const ComPtr<ID3D11ShaderResourceView> oldDiffuse(solid.diffuse);
    cache.resetEnvironments();
    const auto reset = bake(constant.Get());
    return reset.diffuse && reset.diffuse != oldDiffuse.Get() && reset.brdf == solid.brdf;
}

bool checkMeshLighting(ID3D11Device& device, ID3D11DeviceContext& context, sat::ShaderLibrary& shaders) {
    using Microsoft::WRL::ComPtr;
    constexpr auto kTexturePath = "Assets/Environment.png";
    constexpr auto kHdrPath = "Assets/Environment.hdr";
    // Linear radiance 131072, above the maximum finite half-float value.
    constexpr char kHdrPanorama[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n"
        "\x80\x80\x80\x92";
    constexpr auto kMetalPath = "Assets/RoughMetal.satmat";
    // A constant 1x1 sRGB panorama through the normal PNG asset loader.
    constexpr unsigned char kPanorama[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
        0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x68, 0x68, 0x68, 0x00,
        0x00, 0x03, 0x04, 0x01, 0x81, 0x4b, 0xd3, 0xd2, 0x10, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    sat::MeshPass pass;
    pass.setAssetReader([&](std::string_view path, std::string& bytes, std::string&) {
        if (path == kMetalPath) {
            sat::MaterialData metal;
            metal.metallic = 1.0f;
            metal.roughness = 1.0f;
            bytes = sat::encodeMaterial(metal);
            return sat::AssetReadResult::Found;
        }
        if (path == kHdrPath) {
            bytes.assign(kHdrPanorama, sizeof(kHdrPanorama) - 1);
            return sat::AssetReadResult::Found;
        }
        if (path != kTexturePath) { return sat::AssetReadResult::Missing; }
        bytes.assign(reinterpret_cast<const char*>(kPanorama), sizeof(kPanorama));
        return sat::AssetReadResult::Found;
    });
    if (!pass.ensure(&device)) { return false; }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 64;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color, readback, depth;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depthView;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, color.GetAddressOf())) ||
        FAILED(device.CreateRenderTargetView(color.Get(), nullptr, target.GetAddressOf()))) { return false; }
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, readback.GetAddressOf()))) { return false; }
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, depth.GetAddressOf())) ||
        FAILED(device.CreateDepthStencilView(depth.Get(), nullptr, depthView.GetAddressOf()))) { return false; }

    auto* renderTarget = target.Get();
    constexpr float kBlack[4] = {0, 0, 0, 1};
    const D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
    const auto projection = sat::perspective(60, 1, 0.1f, 100);
    sat::MeshInstance litCube;
    sat::SceneDrawList litEnvironment;
    litEnvironment.meshes = &litCube;
    litEnvironment.meshCount = 1;
    litEnvironment.ambient = {};
    litEnvironment.environmentPath = kTexturePath;
    const auto cubeView = sat::lookAt({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    const auto readLitCube = [&](bool sceneView = false) -> std::optional<std::array<int, 3>> {
        context.OMSetRenderTargets(1, &renderTarget, depthView.Get());
        context.RSSetViewports(1, &viewport);
        context.ClearRenderTargetView(target.Get(), kBlack);
        context.ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
        pass.draw(&context, shaders, cubeView, projection, {0, 0, -5}, litEnvironment, true, {}, true,
            nullptr, nullptr, nullptr, 0, sceneView);
        context.OMSetRenderTargets(0, nullptr, nullptr);
        context.CopyResource(readback.Get(), color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context.Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { return std::nullopt; }
        const auto* center = static_cast<const unsigned char*>(mapped.pData) + 32 * mapped.RowPitch + 32 * 4;
        const std::array<int, 3> rgb{center[0], center[1], center[2]};
        context.Unmap(readback.Get(), 0);
        return rgb;
    };
    litEnvironment.environmentIntensity = 0.0f;
    const auto disabled = readLitCube();
    litEnvironment.environmentIntensity = 0.25f;
    const auto low = readLitCube();
    litEnvironment.environmentIntensity = 0.5f;
    const auto high = readLitCube(true);
    litEnvironment.showEnvironmentSky = true;
    const auto withSky = readLitCube();
    if (!disabled || *disabled != std::array<int, 3>{} || !low || !high || high != withSky ||
        std::ranges::max(*low) < 4) {
        std::cerr << "Environment lighting depends on sky visibility or failed to light the mesh\n";
        return false;
    }
    for (std::size_t channel = 0; channel < low->size(); ++channel) {
        if (std::abs((*high)[channel] - 2 * (*low)[channel]) > 2) {
            std::cerr << "Environment intensity did not scale indirect lighting\n";
            return false;
        }
    }
    litCube.materialPath = kMetalPath;
    const auto reflected = readLitCube();
    const auto expectedBrdf = referenceEnvironmentBrdf(127, 127);
    const double expectedReflection = 255.0 * 0.21586 * litEnvironment.environmentIntensity *
        (expectedBrdf[0] + expectedBrdf[1]);
    if (!reflected || std::abs((*reflected)[0] - expectedReflection) > 2.0) {
        std::cerr << "Rough metal did not apply the BRDF lookup to the prefiltered reflection\n";
        return false;
    }

    litEnvironment.environmentPath = kHdrPath;
    litEnvironment.environmentIntensity *= 0.21586f / 131072.0f;
    const auto hdrReflection = readLitCube();
    if (!hdrReflection || std::abs((*hdrReflection)[0] - expectedReflection) > 2.0) {
        std::cerr << "HDR environment lost linear radiance during upload or lighting bake\n";
        return false;
    }

    litCube.materialPath = {};
    litEnvironment.ambient = {0.2f, 0.2f, 0.2f};
    litEnvironment.environmentPath = "Assets/Textures/MissingEnvironment.png";
    const auto missing = readLitCube();
    litEnvironment.environmentPath = {};
    if (!missing || *missing == std::array<int, 3>{} || missing != readLitCube()) {
        std::cerr << "Missing environment did not fall back to scene ambient lighting\n";
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { return 1; }
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const auto createDevice = [&](UINT flags) {
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
            D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), nullptr, context.ReleaseAndGetAddressOf());
    };
    const HRESULT debugDevice = createDevice(D3D11_CREATE_DEVICE_DEBUG);
    if (FAILED(debugDevice) && (debugDevice != DXGI_ERROR_SDK_COMPONENT_MISSING || FAILED(createDevice(0)))) {
        return 1;
    }
    sat::ShaderLibrary shaders;
    shaders.setReader(sat::fileShaderReader(std::filesystem::path(argv[1])));
    if (!shaders.ensure(device.Get())) { return 1; }
    if (!checkEnvironmentLighting(*device.Get(), *context.Get(), shaders)) {
        std::cerr << "Environment bake, BRDF integration or cache validation failed\n";
        return 1;
    }
    if (!checkMeshLighting(*device.Get(), *context.Get(), shaders)) { return 1; }
    ComPtr<ID3D11InfoQueue> messages;
    if (SUCCEEDED(device.As(&messages))) {
        for (UINT64 index = 0; index < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
            SIZE_T size = 0;
            if (FAILED(messages->GetMessage(index, nullptr, &size))) { return 1; }
            std::vector<std::byte> storage(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (FAILED(messages->GetMessage(index, message, &size))) { return 1; }
            if (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION) {
                std::cerr << message->pDescription << '\n';
                return 1;
            }
        }
    }
    return 0;
}
