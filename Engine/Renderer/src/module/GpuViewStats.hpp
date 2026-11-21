#pragma once

#include "GpuScene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sat {

struct GameViewStats;
enum class ViewTimestamp : unsigned { ShadowStart, ShadowEnd, OpaqueEnd, TransmissionEnd, TransparentEnd, PostEnd, Count };

// Readback and timestamp queries never wait or flush the immediate context.
class GpuViewStats {
public:
    [[nodiscard]] bool begin(ID3D11Device& device, ID3D11DeviceContext& context, ID3D11Query* frameInterval,
        GameViewStats& stats);
    void poll(ID3D11DeviceContext& context, ID3D11Query* retiring = nullptr);
    void mark(ID3D11DeviceContext& context, ViewTimestamp point);
    void capture(ID3D11Device& device, ID3D11DeviceContext& context, const GpuScene& scene,
        ID3D11Buffer* args, MeshQueue queue, bool maskShadows);
    void end();

private:
    static constexpr unsigned kPasses = 8; // Four cascades, one spot, two opaque phases, and transmission.
    static constexpr unsigned kTimestamps = static_cast<unsigned>(ViewTimestamp::Count);
    struct BatchSize {
        unsigned triangles, vertices, passes;
        bool masked;
    };
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> frameInterval;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kTimestamps> times;
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        std::array<MeshQueue, kPasses> queues{};
        std::array<bool, kPasses> maskShadows{};
        std::vector<BatchSize> sizes;
        UINT bytes = 0;
        unsigned copies = 0, written = 0;
        bool pending = false;
    };
    [[nodiscard]] bool resolve(ID3D11DeviceContext& context, Slot& slot);
    std::array<Slot, 2> m_slots;
    std::array<std::optional<double>, kTimestamps - 1> m_milliseconds;
    std::uint64_t m_triangles = 0, m_vertices = 0;
    std::size_t m_next = 0;
    bool m_active = false;
};

} // namespace sat
