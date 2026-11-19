#pragma once

#include "CascadedShadowMap.hpp"
#include "GpuScene.hpp"

#include <array>
#include <vector>

namespace sat {

using FrustumPlanes = std::array<std::array<float, 4>, 6>;
[[nodiscard]] FrustumPlanes frustumPlanes(const Mat4& viewProj, bool depthClamp = false);
[[nodiscard]] bool insideFrustum(const FrustumPlanes& planes, Vec3 center, float radius);
[[nodiscard]] Vec3 relativePosition(const GpuInstance& instance, DVec3 origin);

// Each camera owns its history and submission buffers. Cascades have distinct
// argument and visible-index buffers. Camera phases reuse their camera buffers.
class GpuCulling {
public:
    enum class Phase : unsigned { Frustum, FirstOpaque, SecondOpaque };
    [[nodiscard]] bool ensure(ID3D11Device& device, UINT batches, UINT items);
    [[nodiscard]] bool ensureShadows(ID3D11Device& device, UINT batches, UINT items, unsigned count);
    [[nodiscard]] ID3D11Buffer* cull(ID3D11DeviceContext& context, ShaderLibrary& shaders, const GpuScene& scene,
        MeshQueue queue, Phase phase, const FrustumPlanes& planes, DVec3 origin, bool useDepth, unsigned cascade);
    [[nodiscard]] bool buildDepth(ID3D11Device& device, ID3D11DeviceContext& context, ShaderLibrary& shaders,
        ID3D11ShaderResourceView* depth, UINT width, UINT height, const Mat4& viewProj, DVec3 origin);
    [[nodiscard]] bool hasDepth(ID3D11ShaderResourceView* source, UINT width, UINT height) const;
    void invalidateDepth() { m_depth.valid = false; }
    void sampleAnimationOcclusion(ID3D11Device& device, ID3D11DeviceContext& context, ShaderLibrary& shaders,
        AnimationSystem& animation);
private:
    struct RawBuffer {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> view;
        UINT bytes = 0;
        [[nodiscard]] bool ensure(ID3D11Device& device, UINT size, UINT binds, UINT misc, bool exact = false);
    };
    struct Submission {
        RawBuffer args, visible;
        [[nodiscard]] bool ensure(ID3D11Device& device, UINT batches, UINT items);
    };
    struct Depth {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> allLevels;
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> levels;
        std::vector<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>> outputs;
        UINT width = 0, height = 0;
        UINT paddedWidth = 0, paddedHeight = 0;
        Mat4 viewProj;
        DVec3 origin;
        bool valid = false;
        [[nodiscard]] bool ensure(ID3D11Device& device, UINT newWidth, UINT newHeight);
    };
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_cullCb, m_depthCb;
    Submission m_camera;
    std::array<Submission, CascadedShadowMap::kMaxCascades> m_shadows;
    RawBuffer m_rejected, m_retest;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_retestView;
    Depth m_depth;
    bool m_depthAllocationFailed = false;
    struct AnimationReadback {
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        std::vector<AnimationOcclusionProbe> probes;
        AnimationOcclusionStamp stamp;
        UINT bytes = 0;
        bool pending = false;
    };
    std::array<AnimationReadback, 3> m_animationReadbacks;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_animationProbes;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_animationProbeView;
    RawBuffer m_animationResults;
    UINT m_animationProbeCapacity = 0;
    unsigned m_animationReadbackIndex = 0;
    const AnimationSystem* m_animationOwner = nullptr;
};

} // namespace sat
