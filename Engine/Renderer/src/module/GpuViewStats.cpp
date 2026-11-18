#include "GpuViewStats.hpp"

#include "Renderer.hpp"

#include <limits>
#include <optional>
#include <utility>

namespace sat {

bool GpuViewStats::resolve(ID3D11DeviceContext& context, Slot& slot) {
    constexpr UINT kNoFlush = D3D11_ASYNC_GETDATA_DONOTFLUSH;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT interval{};
    const auto status = context.GetData(slot.frameInterval.Get(), &interval, sizeof(interval), kNoFlush);
    if (status == S_FALSE) { return false; }
    if (FAILED(status)) { slot.pending = false; return true; }
    if (slot.copies != 0) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto result = context.Map(slot.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING) { return false; }
        if (SUCCEEDED(result)) {
            const auto* args = static_cast<const GpuBatch*>(mapped.pData);
            m_triangles = m_vertices = 0;
            for (unsigned pass = 0; pass < slot.copies; ++pass) {
                for (const auto& batch : slot.sizes) {
                    if ((batch.passes & meshQueueMask(slot.queues[pass])) != 0 &&
                        (!batch.masked || slot.queues[pass] != MeshQueue::Shadow || slot.maskShadows[pass])) {
                        m_triangles += std::uint64_t{batch.triangles} * args->instanceCount;
                        m_vertices += std::uint64_t{batch.vertices} * args->instanceCount;
                    }
                    ++args;
                }
            }
            context.Unmap(slot.staging.Get(), 0);
        }
    } else {
        m_triangles = m_vertices = 0;
    }

    m_milliseconds = {};
    if (!interval.Disjoint && interval.Frequency != 0) {
        std::array<UINT64, kTimestamps> times{};
        unsigned available = slot.written;
        for (unsigned i = 0; i < times.size(); ++i) {
            if ((available & (1u << i)) != 0 &&
                context.GetData(slot.times[i].Get(), &times[i], sizeof(times[i]), kNoFlush) != S_OK) {
                available &= ~(1u << i);
            }
        }
        for (unsigned i = 0; i < m_milliseconds.size(); ++i) {
            if (((available >> i) & 3u) == 3u && times[i + 1] >= times[i]) {
                m_milliseconds[i] = static_cast<double>(times[i + 1] - times[i]) * 1000.0 /
                    static_cast<double>(interval.Frequency);
            }
        }
    }
    slot.pending = false;
    return true;
}

void GpuViewStats::poll(ID3D11DeviceContext& context, ID3D11Query* retiring) {
    // Resolve in submission order to keep the reported sample monotonic.
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        auto& slot = m_slots[(m_next + i) % m_slots.size()];
        if (slot.pending && !resolve(context, slot)) { break; }
    }
    if (retiring) {
        for (auto& slot : m_slots) {
            if (slot.frameInterval.Get() == retiring) {
                slot.pending = false;
                slot.frameInterval.Reset();
            }
        }
    }
}

bool GpuViewStats::begin(ID3D11Device& device, ID3D11DeviceContext& context, ID3D11Query* frameInterval,
    GameViewStats& stats) {
    poll(context);
    stats.gpuShadowMs = m_milliseconds[0];
    stats.gpuOpaqueMs = m_milliseconds[1];
    stats.gpuTransmissionMs = m_milliseconds[2];
    stats.gpuTransparentMs = m_milliseconds[3];
    stats.gpuPostMs = m_milliseconds[4];
    stats.triangles = m_triangles;
    stats.vertices = m_vertices;

    auto& slot = m_slots[m_next];
    if (slot.pending || !frameInterval) { return false; }
    if (!slot.times.front()) {
        const D3D11_QUERY_DESC timestamp{D3D11_QUERY_TIMESTAMP, 0};
        for (auto& query : slot.times) {
            if (FAILED(device.CreateQuery(&timestamp, query.GetAddressOf()))) {
                slot.times = {};
                return false;
            }
        }
    }
    slot.copies = slot.written = 0;
    slot.sizes.clear();
    // Use the frame's existing disjoint interval for all pass timestamps.
    slot.frameInterval = frameInterval;
    m_active = true;
    return true;
}

void GpuViewStats::mark(ID3D11DeviceContext& context, ViewTimestamp point) {
    if (!m_active) { return; }
    auto& slot = m_slots[m_next];
    const auto index = static_cast<unsigned>(point);
    context.End(slot.times[index].Get());
    slot.written |= 1u << index;
}

void GpuViewStats::capture(ID3D11Device& device, ID3D11DeviceContext& context, const GpuScene& scene,
    ID3D11Buffer* args, MeshQueue queue, bool maskShadows) {
    if (!m_active || scene.batches().empty()) { return; }
    auto& slot = m_slots[m_next];
    if (slot.copies >= kPasses || scene.batches().size() > std::numeric_limits<UINT>::max() / sizeof(GpuBatch) / kPasses) {
        return;
    }
    const auto bytes = static_cast<UINT>(scene.batches().size() * sizeof(GpuBatch));
    if (slot.bytes < bytes * kPasses) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = bytes * kPasses;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        if (FAILED(device.CreateBuffer(&desc, nullptr, staging.GetAddressOf()))) { return; }
        slot.staging = std::move(staging);
        slot.bytes = desc.ByteWidth;
    }
    if (slot.copies == 0) {
        slot.sizes.reserve(scene.batches().size());
        for (const auto& batch : scene.batches()) {
            slot.sizes.push_back({batch.indexCount / 3, batch.vertexCount, batch.passes,
                batch.material->alphaMode == AlphaMode::Mask});
        }
    }
    const D3D11_BOX source{0, 0, 0, bytes, 1, 1};
    context.CopySubresourceRegion(slot.staging.Get(), 0, slot.copies * bytes, 0, 0, args, 0, &source);
    slot.queues[slot.copies] = queue;
    slot.maskShadows[slot.copies++] = maskShadows;
}

void GpuViewStats::end() {
    if (!m_active) { return; }
    auto& slot = m_slots[m_next];
    slot.pending = true;
    m_next = (m_next + 1) % m_slots.size();
    m_active = false;
}

} // namespace sat
