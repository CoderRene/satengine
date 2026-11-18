#include "GpuCulling.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <utility>

namespace sat {
namespace {

constexpr unsigned kCullThreads = 64;
constexpr unsigned kDepthThreads = 8;
struct alignas(16) CullCb {
    FrustumPlanes planes;
    Mat4 hiZViewProj;
    float originHigh[4], originLow[4], hiZOriginHigh[4], hiZOriginLow[4];
    unsigned itemCount, firstItem, phase, hiZLevels;
    unsigned width, height, paddedWidth, paddedHeight;
};
struct alignas(16) DepthCb {
    unsigned sourceWidth, sourceHeight, targetWidth, targetHeight;
};
static_assert(sizeof(CullCb) == 256 && sizeof(DepthCb) == 16);

bool ensureConstants(ID3D11Device& device, UINT bytes, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer) {
    if (buffer) { return true; }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return SUCCEEDED(device.CreateBuffer(&desc, nullptr, buffer.GetAddressOf()));
}

} // namespace

FrustumPlanes frustumPlanes(const Mat4& viewProj, bool depthClamp) {
    FrustumPlanes result{};
    for (unsigned col = 0; col < 4; ++col) {
        const auto* values = viewProj.m + col * 4;
        result[0][col] = values[3] + values[0];
        result[1][col] = values[3] - values[0];
        result[2][col] = values[3] + values[1];
        result[3][col] = values[3] - values[1];
        result[4][col] = depthClamp ? 0.0f : values[2];
        result[5][col] = values[3] - values[2];
    }
    for (auto& plane : result) {
        const float magnitude = length(Vec3{plane[0], plane[1], plane[2]});
        if (magnitude > 0.0f) {
            for (auto& value : plane) { value /= magnitude; }
        }
    }
    return result;
}

bool insideFrustum(const FrustumPlanes& planes, Vec3 center, float radius) {
    if (radius < 0) { return true; }
    return std::none_of(planes.begin(), planes.end(), [&](const auto& plane) {
        return dot(Vec3{plane[0], plane[1], plane[2]}, center) + plane[3] < -radius;
    });
}

Vec3 relativePosition(const GpuInstance& instance, DVec3 origin) {
    return toVec3(DVec3{
        double(instance.translationHigh[0]) + double(instance.translationLow[0]) - origin.x,
        double(instance.translationHigh[1]) + double(instance.translationLow[1]) - origin.y,
        double(instance.translationHigh[2]) + double(instance.translationLow[2]) - origin.z});
}

bool GpuCulling::RawBuffer::ensure(ID3D11Device& device, UINT size, UINT binds, UINT misc, bool exact) {
    if (exact ? size == bytes : size <= bytes) { return true; }
    const UINT capacity = exact ? size : std::bit_ceil(size);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = capacity;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = binds | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = misc | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
    output.Format = DXGI_FORMAT_R32_TYPELESS;
    output.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    output.Buffer.NumElements = capacity / static_cast<UINT>(sizeof(unsigned));
    output.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    RawBuffer candidate;
    if (FAILED(device.CreateBuffer(&desc, nullptr, candidate.buffer.GetAddressOf())) ||
        FAILED(device.CreateUnorderedAccessView(candidate.buffer.Get(), &output, candidate.view.GetAddressOf()))) {
        return false;
    }
    candidate.bytes = capacity;
    *this = std::move(candidate);
    return true;
}

bool GpuCulling::Submission::ensure(ID3D11Device& device, UINT batches, UINT items) {
    return args.ensure(device, batches * static_cast<UINT>(sizeof(GpuBatch)), 0,
            D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, true) &&
        visible.ensure(device, items * static_cast<UINT>(sizeof(unsigned)), D3D11_BIND_VERTEX_BUFFER, 0);
}

bool GpuCulling::ensureShadows(ID3D11Device& device, UINT batches, UINT items, unsigned count) {
    if (items == 0) { return true; }
    for (unsigned i = 0; i < std::min(count, CascadedShadowMap::kMaxCascades); ++i) {
        if (!m_shadows[i].ensure(device, batches, items)) { return false; }
    }
    return true;
}

bool GpuCulling::ensure(ID3D11Device& device, UINT batches, UINT items) {
    if (batches == 0) { return true; }
    if (!m_retestView) {
        if (!m_retest.ensure(device, sizeof(unsigned) * 4, D3D11_BIND_SHADER_RESOURCE,
                D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, true)) { return false; }
        D3D11_SHADER_RESOURCE_VIEW_DESC input{};
        input.Format = DXGI_FORMAT_R32_TYPELESS;
        input.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        input.BufferEx.NumElements = 4;
        input.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        if (FAILED(device.CreateShaderResourceView(m_retest.buffer.Get(), &input, m_retestView.GetAddressOf()))) {
            return false;
        }
    }
    return ensureConstants(device, sizeof(CullCb), m_cullCb) &&
        m_camera.ensure(device, batches, items) &&
        m_rejected.ensure(device, items * static_cast<UINT>(sizeof(unsigned)), 0, 0);
}

ID3D11Buffer* GpuCulling::cull(ID3D11DeviceContext& context, ShaderLibrary& shaders, const GpuScene& scene,
    MeshQueue queue, Phase phase, const FrustumPlanes& planes, DVec3 origin, bool useDepth, unsigned cascade) {
    const auto range = scene.itemRange(queue);
    if (range.count == 0) { return nullptr; }
    auto& submission = queue == MeshQueue::Shadow ? m_shadows[cascade] : m_camera;
    ID3D11Buffer* noBuffer = nullptr;
    const UINT zero = 0;
    context.IASetVertexBuffers(1, 1, &noBuffer, &zero, &zero);
    context.CopyResource(submission.args.buffer.Get(), scene.argsTemplate());

    CullCb constants{};
    constants.planes = planes;
    splitOrigin(origin, constants.originHigh, constants.originLow);
    const bool firstOpaque = phase == Phase::FirstOpaque && useDepth;
    const bool retest = phase == Phase::SecondOpaque;
    if (firstOpaque) {
        const unsigned dispatch[]{0, 1, 1, 0};
        context.UpdateSubresource(m_retest.buffer.Get(), 0, nullptr, dispatch, 0, 0);
    }
    constants.itemCount = range.count;
    constants.firstItem = range.start;
    constants.phase = firstOpaque || retest ? static_cast<unsigned>(phase) : 0;
    if (useDepth && m_depth.valid) {
        constants.hiZViewProj = m_depth.viewProj;
        splitOrigin(m_depth.origin, constants.hiZOriginHigh, constants.hiZOriginLow);
        constants.hiZLevels = static_cast<unsigned>(m_depth.levels.size());
        constants.width = m_depth.width;
        constants.height = m_depth.height;
        constants.paddedWidth = m_depth.paddedWidth;
        constants.paddedHeight = m_depth.paddedHeight;
    }
    context.UpdateSubresource(m_cullCb.Get(), 0, nullptr, &constants, 0, 0);
    ID3D11ShaderResourceView* inputs[]{scene.instancesView(), scene.itemsView(),
        constants.hiZLevels ? m_depth.allLevels.Get() : nullptr, retest ? m_retestView.Get() : nullptr};
    ID3D11UnorderedAccessView* outputs[]{submission.args.view.Get(), submission.visible.view.Get(),
        firstOpaque || retest ? m_rejected.view.Get() : nullptr, firstOpaque ? m_retest.view.Get() : nullptr};
    context.CSSetShaderResources(0, 4, inputs);
    context.CSSetUnorderedAccessViews(0, 4, outputs, nullptr);
    context.CSSetConstantBuffers(0, 1, m_cullCb.GetAddressOf());
    context.CSSetShader(shaders.compute(ComputeShaderId::CullInstances), nullptr, 0);
    if (retest) { context.DispatchIndirect(m_retest.buffer.Get(), 0); }
    else { context.Dispatch((constants.itemCount + kCullThreads - 1) / kCullThreads, 1, 1); }
    ID3D11ShaderResourceView* nullInputs[4]{};
    ID3D11UnorderedAccessView* nullOutputs[4]{};
    context.CSSetShaderResources(0, 4, nullInputs);
    context.CSSetUnorderedAccessViews(0, 4, nullOutputs, nullptr);
    context.CSSetShader(nullptr, nullptr, 0);

    const UINT stride = sizeof(unsigned);
    context.IASetVertexBuffers(1, 1, submission.visible.buffer.GetAddressOf(), &stride, &zero);
    return submission.args.buffer.Get();
}

bool GpuCulling::Depth::ensure(ID3D11Device& device, UINT newWidth, UINT newHeight) {
    if (width == newWidth && height == newHeight && allLevels) { return true; }
    Depth candidate;
    candidate.width = newWidth;
    candidate.height = newHeight;
    candidate.paddedWidth = std::bit_ceil((newWidth + 1) / 2);
    candidate.paddedHeight = std::bit_ceil((newHeight + 1) / 2);
    const UINT count = std::bit_width(std::max(candidate.paddedWidth, candidate.paddedHeight));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = candidate.paddedWidth;
    desc.Height = candidate.paddedHeight;
    desc.MipLevels = count;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device.CreateTexture2D(&desc, nullptr, texture.GetAddressOf())) ||
        FAILED(device.CreateShaderResourceView(texture.Get(), nullptr, candidate.allLevels.GetAddressOf()))) { return false; }
    candidate.levels.resize(count);
    candidate.outputs.resize(count);
    for (UINT i = 0; i < count; ++i) {
        D3D11_SHADER_RESOURCE_VIEW_DESC input{};
        input.Format = desc.Format;
        input.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        input.Texture2D.MostDetailedMip = i;
        input.Texture2D.MipLevels = 1;
        D3D11_UNORDERED_ACCESS_VIEW_DESC output{};
        output.Format = desc.Format;
        output.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        output.Texture2D.MipSlice = i;
        if (FAILED(device.CreateShaderResourceView(texture.Get(), &input, candidate.levels[i].GetAddressOf())) ||
            FAILED(device.CreateUnorderedAccessView(texture.Get(), &output, candidate.outputs[i].GetAddressOf()))) { return false; }
    }
    *this = std::move(candidate);
    return true;
}

bool GpuCulling::hasDepth(ID3D11ShaderResourceView* source, UINT width, UINT height) const {
    return m_depth.valid && m_depth.source.Get() == source && m_depth.width == width && m_depth.height == height;
}

bool GpuCulling::buildDepth(ID3D11Device& device, ID3D11DeviceContext& context, ShaderLibrary& shaders,
    ID3D11ShaderResourceView* depth, UINT width, UINT height, const Mat4& viewProj, DVec3 origin) {
    m_depth.valid = false;
    if (!depth) { return false; }
    if (!m_depth.ensure(device, width, height) || !ensureConstants(device, sizeof(DepthCb), m_depthCb)) {
        if (!m_depthAllocationFailed) { std::cerr << "Cannot allocate mesh occlusion depth pyramid\n"; }
        m_depthAllocationFailed = true;
        return false;
    }
    m_depthAllocationFailed = false;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> color;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget;
    context.OMGetRenderTargets(1, color.GetAddressOf(), depthTarget.GetAddressOf());
    context.OMSetRenderTargets(0, nullptr, nullptr);
    context.CSSetShader(shaders.compute(ComputeShaderId::DepthPyramid), nullptr, 0);
    context.CSSetConstantBuffers(0, 1, m_depthCb.GetAddressOf());
    UINT sourceWidth = width, sourceHeight = height;
    ID3D11ShaderResourceView* nullInput = nullptr;
    ID3D11UnorderedAccessView* nullOutput = nullptr;
    for (UINT i = 0; i < m_depth.outputs.size(); ++i) {
        const UINT targetWidth = std::max(1u, m_depth.paddedWidth >> i);
        const UINT targetHeight = std::max(1u, m_depth.paddedHeight >> i);
        const DepthCb constants{sourceWidth, sourceHeight, targetWidth, targetHeight};
        context.UpdateSubresource(m_depthCb.Get(), 0, nullptr, &constants, 0, 0);
        auto* input = i == 0 ? depth : m_depth.levels[i - 1].Get();
        context.CSSetShaderResources(0, 1, &input);
        context.CSSetUnorderedAccessViews(0, 1, m_depth.outputs[i].GetAddressOf(), nullptr);
        context.Dispatch((targetWidth + kDepthThreads - 1) / kDepthThreads,
            (targetHeight + kDepthThreads - 1) / kDepthThreads, 1);
        context.CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
        context.CSSetShaderResources(0, 1, &nullInput);
        sourceWidth = targetWidth;
        sourceHeight = targetHeight;
    }
    context.CSSetShader(nullptr, nullptr, 0);
    context.OMSetRenderTargets(1, color.GetAddressOf(), depthTarget.Get());
    m_depth.source = depth;
    m_depth.viewProj = viewProj;
    m_depth.origin = origin;
    m_depth.valid = true;
    return true;
}

} // namespace sat
