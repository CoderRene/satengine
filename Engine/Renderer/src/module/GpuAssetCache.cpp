#include "GpuAssetCache.hpp"

#include "AssetImporter.hpp"
#include "CookedTexture.hpp"
#include "Math.hpp"
#include "ShaderLayout.hpp"
#include "module/RenderBenchmark.hpp"

#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <iterator>
#include <list>
#include <mutex>
#include <numeric>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sat {
namespace {
using Microsoft::WRL::ComPtr;
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
};
struct TextureResource {
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11ShaderResourceView> srgb;
    std::size_t sampler = 0;
};
struct MaterialResource {
    ComPtr<ID3D11Buffer> constants;
    GpuMaterial view;
    std::vector<std::string> textures;
};
using Textures = std::unordered_map<std::string, TextureResource, StringHash, std::equal_to<>>;
// Pages never move. Returning a range coalesces adjacent free ranges without
// allocation. Uploads and draws are ordered on the same immediate context.
class GeometryPool {
    struct Range { UINT first, count; bool free; };
    struct Page {
        ComPtr<ID3D11Buffer> buffer;
        std::list<Range> ranges;
    };
public:
    class Allocation {
    public:
        Allocation() = default;
        Allocation(std::shared_ptr<Page> page, std::list<Range>::iterator range)
            : m_page(std::move(page)), m_range(range) {}
        Allocation(Allocation&& other) noexcept
            : m_page(std::move(other.m_page)), m_range(other.m_range) {}
        Allocation& operator=(Allocation&& other) noexcept {
            if (this != &other) {
                release();
                m_page = std::move(other.m_page);
                m_range = other.m_range;
            }
            return *this;
        }
        ~Allocation() { release(); }

        [[nodiscard]] ID3D11Buffer* buffer() const { return m_page ? m_page->buffer.Get() : nullptr; }
        [[nodiscard]] UINT first() const { return m_range->first; }

    private:
        void release() noexcept {
            if (!m_page) { return; }
            auto& ranges = m_page->ranges;
            m_range->free = true;
            auto next = std::next(m_range);
            if (next != ranges.end() && next->free) {
                m_range->count += next->count;
                ranges.erase(next);
            }
            if (m_range != ranges.begin()) {
                const auto previous = std::prev(m_range);
                if (previous->free) {
                    previous->count += m_range->count;
                    ranges.erase(m_range);
                }
            }
        }
        std::shared_ptr<Page> m_page;
        std::list<Range>::iterator m_range{};
    };

    [[nodiscard]] Allocation upload(ID3D11Device& device, ID3D11DeviceContext& context,
        const void* data, UINT count, UINT stride, UINT binds) {
        const auto commit = [&](const std::shared_ptr<Page>& page, std::list<Range>::iterator range) {
            if (range->count > count) {
                page->ranges.insert(std::next(range), {range->first + count, range->count - count, true});
                range->count = count;
            }
            range->free = false;
            const D3D11_BOX box{range->first * stride, 0, 0, (range->first + count) * stride, 1, 1};
            context.UpdateSubresource(page->buffer.Get(), 0, &box, data, 0, 0);
            return Allocation(page, range);
        };
        for (const auto& page : m_pages) {
            const auto range = std::ranges::find_if(page->ranges,
                [count](const Range& entry) { return entry.free && entry.count >= count; });
            if (range != page->ranges.end()) { return commit(page, range); }
        }
        constexpr UINT kPageBytes = 8 * 1024 * 1024;
        const UINT capacity = std::max(count, kPageBytes / stride);
        auto page = std::make_shared<Page>();
        D3D11_BUFFER_DESC desc{};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.ByteWidth = capacity * stride;
        desc.BindFlags = binds;
        if (FAILED(device.CreateBuffer(&desc, nullptr, page->buffer.GetAddressOf()))) { return {}; }
        page->ranges.push_back({0, capacity, true});
        // An oversized mesh gets its own page, released with that mesh.
        if (count <= kPageBytes / stride) { m_pages.push_back(page); }
        return commit(page, page->ranges.begin());
    }

private:
    std::vector<std::shared_ptr<Page>> m_pages;
};

struct MeshResource {
    GeometryPool::Allocation pooledVertices, pooledIndices;
    ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11Buffer> skin;
    ComPtr<ID3D11Buffer> indices;
    ComPtr<ID3D11ShaderResourceView> skinVertices, skinWeights;
    GpuMesh view;
    Vec3 minimum, maximum;
    Textures embeddedTextures;
    std::vector<MaterialData> sourceMaterials;
    std::vector<std::string> materialAssetPaths;
    std::vector<std::string> dependencies;
    std::vector<MaterialResource> materials;
    bool materialsReady = false;
};
HRESULT createRawView(ID3D11Device& device, ID3D11Buffer* buffer, UINT bytes, ID3D11ShaderResourceView** view) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.NumElements = bytes / 4;
    desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    return device.CreateShaderResourceView(buffer, &desc, view);
}

bool createMesh(ID3D11Device* device, ID3D11DeviceContext* context, GeometryPool& vertexPool,
    GeometryPool& indexPool, const MeshData& data, MeshResource& resource, std::string& error, bool thumbnail) {
    // A whole-model thumbnail only draws the main index ranges.
    const auto nodeIndices = thumbnail ? std::span<const std::uint32_t>{} : std::span(data.nodeIndices);
    const auto nodeSections = thumbnail ? std::span<const MeshSection>{} : std::span(data.nodeSections);
    const auto nodes = thumbnail ? std::span<const MeshNode>{} : std::span(data.nodes);
    constexpr auto kIndexCapacity = kMaxMeshBufferBytes / sizeof(std::uint32_t);
    if (data.vertices.empty() || (data.indices.empty() && nodeIndices.empty()) ||
        data.vertices.size() > kMaxMeshBufferBytes / sizeof(MeshVertex) ||
        data.indices.size() > kIndexCapacity || nodeIndices.size() > kIndexCapacity - data.indices.size()) {
        error = "Invalid GPU mesh buffer size";
        return false;
    }
    std::vector<std::uint32_t> combined;
    if (!nodeIndices.empty()) {
        combined.reserve(data.indices.size() + nodeIndices.size());
        combined.insert(combined.end(), data.indices.begin(), data.indices.end());
        combined.insert(combined.end(), nodeIndices.begin(), nodeIndices.end());
    }
    const auto& indices = combined.empty() ? data.indices : combined;
    const auto vertexCount = static_cast<UINT>(data.vertices.size());
    const auto vertexBytes = static_cast<UINT>(data.vertices.size() * sizeof(MeshVertex));
    const auto indexCount = static_cast<UINT>(indices.size());
    if (context && data.skinVertices.empty()) {
        resource.pooledVertices = vertexPool.upload(*device, *context, data.vertices.data(), vertexCount,
            sizeof(MeshVertex), D3D11_BIND_VERTEX_BUFFER);
        if (resource.pooledVertices.buffer()) {
            resource.pooledIndices = indexPool.upload(*device, *context, indices.data(), indexCount,
                sizeof(std::uint32_t), D3D11_BIND_INDEX_BUFFER);
        }
    }
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA initial{};
    auto* pooledIndices = resource.pooledIndices.buffer();
    if (pooledIndices) {
        resource.view.vertexBuffer = resource.pooledVertices.buffer();
        resource.view.indexBuffer = pooledIndices;
        resource.view.baseVertex = static_cast<INT>(resource.pooledVertices.first());
        resource.view.firstIndex = resource.pooledIndices.first();
    } else {
        resource.pooledVertices = {};
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        if (!data.skinVertices.empty()) {
            desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        }
        desc.ByteWidth = vertexBytes;
        initial.pSysMem = data.vertices.data();
        HRESULT hr = device->CreateBuffer(&desc, &initial, resource.vertices.GetAddressOf());
        if (SUCCEEDED(hr)) {
            desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            desc.MiscFlags = 0;
            desc.ByteWidth = indexCount * sizeof(std::uint32_t);
            initial.pSysMem = indices.data();
            hr = device->CreateBuffer(&desc, &initial, resource.indices.GetAddressOf());
        }
        if (FAILED(hr)) { error = "Cannot create GPU mesh buffers (HRESULT " + std::to_string(hr) + ")"; return false; }
        resource.view.vertexBuffer = resource.vertices.Get();
        resource.view.indexBuffer = resource.indices.Get();
    }
    if (!data.skinVertices.empty()) {
        if (data.skinVertices.size() != data.vertices.size()) { error = "Skin vertex count mismatch"; return false; }
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        desc.ByteWidth = static_cast<UINT>(data.skinVertices.size() * sizeof(MeshSkinVertex));
        initial.pSysMem = data.skinVertices.data();
        if (FAILED(device->CreateBuffer(&desc, &initial, resource.skin.GetAddressOf())) ||
            FAILED(createRawView(*device, resource.skin.Get(), desc.ByteWidth, resource.skinWeights.GetAddressOf())) ||
            FAILED(createRawView(*device, resource.vertices.Get(), vertexBytes,
                resource.skinVertices.GetAddressOf()))) {
            error = "Cannot create raw skinning buffers"; return false;
        }
        resource.view.skinVertices = resource.skinVertices.Get();
        resource.view.skinWeights = resource.skinWeights.Get();
        for (std::size_t joint = 0; joint < std::min(data.restPalette.size(), data.jointBounds.size()); ++joint) {
            includeSkinnedBounds(resource.view.restBounds, data.jointBounds[joint], data.restPalette[joint]);
        }
        resource.view.restPalette = data.restPalette;
    }
    resource.view.indexCount = static_cast<UINT>(data.indices.size());
    resource.view.vertexCount = static_cast<UINT>(data.vertices.size());
    resource.view.vertexStride = static_cast<UINT>(sizeof(MeshVertex));
    resource.view.indexFormat = DXGI_FORMAT_R32_UINT;
    resource.view.mainSectionCount = static_cast<UINT>(data.sections.size());
    resource.view.mergeNodeDraws = data.mergeNodeDraws;
    resource.view.sections.reserve(data.sections.size() + nodeSections.size());
    // Cache each section's distinct vertex count at upload time.
    std::vector<std::uint32_t> lastSection;
    if (!thumbnail && (!data.sections.empty() || !nodeSections.empty())) {
        lastSection.resize(data.vertices.size());
    }
    const auto appendSections = [&](
        std::span<const MeshSection> ranges, std::span<const std::uint32_t> indices, UINT offset) {
        for (const auto& range : ranges) {
            auto& section = resource.view.sections.emplace_back(GpuMeshSection{range});
            section.range.firstIndex += offset;
            if (!lastSection.empty()) {
                const auto sectionId = static_cast<std::uint32_t>(resource.view.sections.size());
                for (std::uint32_t i = range.firstIndex, end = range.firstIndex + range.indexCount; i < end; ++i) {
                    auto& last = lastSection[indices[i]];
                    if (last != sectionId) {
                        last = sectionId;
                        ++section.vertexCount;
                    }
                }
            }
            if (range.indexCount == 0 || range.materialIndex >= data.materials.size() ||
                data.materials[range.materialIndex].alphaMode != AlphaMode::Blend) { continue; }
            const auto& first = data.vertices[indices[range.firstIndex]];
            float minimum[] = {first.x, first.y, first.z};
            float maximum[] = {first.x, first.y, first.z};
            for (std::uint32_t i = range.firstIndex + 1, end = range.firstIndex + range.indexCount; i < end; ++i) {
                const auto& vertex = data.vertices[indices[i]];
                const float position[] = {vertex.x, vertex.y, vertex.z};
                for (int axis = 0; axis < 3; ++axis) {
                    minimum[axis] = std::min(minimum[axis], position[axis]);
                    maximum[axis] = std::max(maximum[axis], position[axis]);
                }
            }
            for (int axis = 0; axis < 3; ++axis) { section.center[axis] = std::midpoint(minimum[axis], maximum[axis]); }
        }
    };
    appendSections(data.sections, data.indices, 0);
    appendSections(nodeSections, nodeIndices, resource.view.indexCount);
    const UINT sectionOffset = nodeSections.empty() ? 0 : resource.view.mainSectionCount;
    resource.view.nodes.reserve(nodes.size());
    for (const auto& source : nodes) {
        if (source.sectionCount == 0) { continue; }
        auto& node = resource.view.nodes.emplace_back();
        node.id = source.id;
        node.firstSection = source.firstSection + sectionOffset;
        node.sectionCount = source.sectionCount;
        node.firstIndex = resource.view.sections[node.firstSection].range.firstIndex;
        node.origin = {source.origin[0], source.origin[1], source.origin[2]};
        node.minimum = {source.minimum[0], source.minimum[1], source.minimum[2]};
        node.maximum = {source.maximum[0], source.maximum[1], source.maximum[2]};
        for (UINT i = node.firstSection, end = i + node.sectionCount; i < end; ++i) {
            node.indexCount += resource.view.sections[i].range.indexCount;
            node.vertexCount += resource.view.sections[i].vertexCount;
        }
    }
    std::sort(resource.view.nodes.begin(), resource.view.nodes.end(),
        [](const auto& a, const auto& b) { return a.id < b.id; });
    float extent[3];
    for (int axis = 0; axis < 3; ++axis) {
        const float center = std::midpoint(data.minimum[axis], data.maximum[axis]);
        resource.view.boundsCenter[axis] = center;
        extent[axis] = std::max(center - data.minimum[axis], data.maximum[axis] - center);
    }
    resource.view.boundsRadius = length({extent[0], extent[1], extent[2]});
    resource.minimum = {data.minimum[0], data.minimum[1], data.minimum[2]};
    resource.maximum = {data.maximum[0], data.maximum[1], data.maximum[2]};
    return true;
}
bool createTexture(ID3D11Device* device, const TextureData& data, TextureResource& resource, std::string& error) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(data.width);
    desc.Height = static_cast<UINT>(data.height);
    desc.MipLevels = static_cast<UINT>(data.mips.size() + 1);
    desc.ArraySize = 1;
    const bool hdr = !data.hdrPixels.empty();
    const UINT bytesPerPixel = hdr ? 16 : 4;
    desc.Format = data.blockCompressed ? DXGI_FORMAT_BC7_TYPELESS :
        hdr ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> initial(desc.MipLevels);
    initial[0].pSysMem = hdr ? static_cast<const void*>(data.hdrPixels.data()) : data.pixels.data();
    const auto rowPitch = [&](int width) {
        return data.blockCompressed ? static_cast<UINT>((width + 3) / 4) * 16 : static_cast<UINT>(width) * bytesPerPixel;
    };
    initial[0].SysMemPitch = rowPitch(data.width);
    for (std::size_t i = 0; i < data.mips.size(); ++i) {
        const auto& mip = data.mips[i];
        initial[i + 1].pSysMem = hdr ? static_cast<const void*>(mip.hdrPixels.data()) : mip.pixels.data();
        initial[i + 1].SysMemPitch = rowPitch(mip.width);
    }
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&desc, initial.data(), texture.GetAddressOf());
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = data.blockCompressed ? DXGI_FORMAT_BC7_UNORM : hdr ? desc.Format : DXGI_FORMAT_R8G8B8A8_UNORM;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = desc.MipLevels;
    if (SUCCEEDED(hr)) { hr = device->CreateShaderResourceView(texture.Get(), &view, resource.view.GetAddressOf()); }
    if (FAILED(hr)) { error = "Cannot create GPU texture (HRESULT " + std::to_string(hr) + ")"; return false; }
    if (hdr) { resource.srgb = resource.view; } // The HDR view already samples in linear space.
    resource.sampler = static_cast<std::size_t>(data.filter) * 2 + static_cast<std::size_t>(data.wrap);
    return true;
}
}
struct GpuAssetCache::Impl {
    AssetLibrary library;
    AssetReader thumbnailReader; // Set only while preparing a standalone thumbnail.
    static constexpr int kThumbnailTextureSize = 128;
    GeometryPool vertexPool, indexPool;
    std::unordered_map<std::string, MeshResource, StringHash, std::equal_to<>> meshes;
    Textures textures;
    std::array<ComPtr<ID3D11SamplerState>, 6> samplers;
    std::array<ComPtr<ID3D11SamplerState>, 3 * 3 * 16> materialSamplers;
    std::unordered_map<std::string, MaterialResource, StringHash, std::equal_to<>> materials;
    static constexpr std::size_t kMissingBaseColor = 3;
    std::array<TextureResource, kMissingBaseColor + 1> defaults;
    bool defaultsReady = false;
    bool defaultsFailed = false;
    std::vector<std::string> errors;
    std::size_t reportedCpuErrors = 0;
    bool limitReported = false;

    struct DecodeResult {
        PreparedAssetImport asset;
        std::vector<std::string> dependencies;
        std::string error;
        bool preview = false;
        bool measured = false;
        RenderBenchmark::Clock::duration readTime{};
        RenderBenchmark::Clock::duration decodeTime{};
    };
    struct DecodeJob {
        std::string path;
        std::future<DecodeResult> result;
        bool discard = false;
    };

    static constexpr unsigned kMaxDecodeWorkers = 3;
    static constexpr auto kUploadBudget = std::chrono::milliseconds(2);
    AssetReader workerReader;
    std::deque<std::string> requests;
    std::unordered_set<std::string, StringHash, std::equal_to<>> scheduled;
    std::unordered_map<std::string, std::vector<std::string>, StringHash, std::equal_to<>> failedDependencies;
    std::unordered_set<std::string, StringHash, std::equal_to<>> reloads;
    std::unordered_set<std::string, StringHash, std::equal_to<>> refinements;
    // The render thread owns request and result state. Each job occupies one
    // slot until consumed, bounding both active work and decoded results.
    std::deque<DecodeJob> inFlight;
    std::mutex workerMutex;
    std::condition_variable_any workerReady;
    std::deque<std::packaged_task<DecodeResult()>> workerTasks;
    // Joins before the queue, synchronization, and reader state are released.
    std::vector<std::jthread> workers;

    void enqueue(std::string_view path) {
        if (scheduled.size() < 4096 && scheduled.emplace(path).second) { requests.emplace_back(path); }
    }
    void cancel(std::string_view path) {
        for (auto& job : inFlight) {
            if (job.path == path) { job.discard = true; }
        }
        std::erase(requests, path);
        if (const auto found = refinements.find(path); found != refinements.end()) { refinements.erase(found); }
        if (const auto found = reloads.find(path); found != reloads.end()) { reloads.erase(found); }
        if (const auto found = scheduled.find(path); found != scheduled.end()) { scheduled.erase(found); }
    }
    void startDecodes(bool allowReload) {
        if (requests.empty()) { return; }
        if (workers.empty()) {
            const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
            const unsigned count = std::min(kMaxDecodeWorkers, cores - 1);
            workers.reserve(count);
            for (unsigned i = 0; i < count; ++i) {
                workers.emplace_back([this](std::stop_token stop) {
                    std::unique_lock lock(workerMutex);
                    while (workerReady.wait(lock, stop, [this] { return !workerTasks.empty(); })) {
                        if (stop.stop_requested()) { break; }
                        auto work = std::move(workerTasks.front());
                        workerTasks.pop_front();
                        lock.unlock();
                        work();
                        lock.lock();
                    }
                });
            }
        }

        auto next = requests.begin();
        while (inFlight.size() < workers.size()) {
            const auto eligible = [&](const std::string& path) {
                return allowReload || !reloads.contains(path);
            };
            // Geometry gets the next available worker even when texture
            // refinements from previously loaded objects are queued.
            next = std::find_if(requests.begin(), requests.end(), [&](const std::string& path) {
                return isMeshAssetPath(path) && eligible(path);
            });
            if (next == requests.end()) {
                next = std::find_if(requests.begin(), requests.end(), [&](const std::string& path) {
                    return !refinements.contains(path) && eligible(path);
                });
            }
            if (next == requests.end()) { next = std::find_if(requests.begin(), requests.end(), eligible); }
            if (next == requests.end()) { break; }
            const bool tryPreview = !refinements.contains(*next) && !reloads.contains(*next);
            const bool measure = RenderBenchmark::captureAssets() && isTextureAssetPath(*next);
            std::packaged_task<DecodeResult()> work([reader = workerReader, path = *next, tryPreview, measure] {
                using Clock = RenderBenchmark::Clock;
                DecodeResult result;
                result.measured = measure;
                const auto start = measure ? Clock::now() : Clock::time_point{};
                const auto read = [&](std::string_view path, std::string& bytes, std::string& error) {
                    const auto before = measure ? Clock::now() : Clock::time_point{};
                    const auto status = reader(path, bytes, error);
                    if (measure) { result.readTime += Clock::now() - before; }
                    return status;
                };
                if (isTextureAssetPath(path)) {
                    std::string bytes;
                    const auto readCooked = [&](bool preview) {
                        return read(cookedTexturePath(path, preview), bytes, result.error) == AssetReadResult::Found &&
                            decodeCookedTexture(bytes, result.asset.texture, result.error);
                    };
                    result.preview = tryPreview && readCooked(true);
                    if (result.preview || readCooked(false)) {
                        result.asset.path = path;
                        if (measure) { result.decodeTime = Clock::now() - start - result.readTime; }
                        return result;
                    }
                }
                const AssetReader track = [&](std::string_view dependency, std::string& bytes, std::string& error) {
                    if (dependency != path) { result.dependencies.emplace_back(dependency); }
                    return read(dependency, bytes, error);
                };
                if (!decodeAssetImport(track, path, nullptr, result.asset, result.error)) {
                    if (result.error.empty()) { result.error = "Cannot decode asset"; }
                } else if (result.asset.isMesh) {
                    prepareEmbeddedTextures(reader, path, result.asset);
                }
                if (measure) { result.decodeTime = Clock::now() - start - result.readTime; }
                return result;
            });
            {
                const std::lock_guard lock(workerMutex);
                inFlight.push_back({*next, work.get_future()});
                try {
                    workerTasks.push_back(std::move(work));
                } catch (...) {
                    inFlight.pop_back();
                    throw;
                }
            }
            next = requests.erase(next);
            workerReady.notify_one();
        }
    }
    void reportImportError(std::string_view path, const std::string& error) {
        std::erase_if(errors, [path](const std::string& message) {
            return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
        });
        if (!error.empty()) { errors.push_back(std::string(path) + ": " + error); }
    }

    void reportCpuErrors() {
        const auto& source = library.errors();
        for (; reportedCpuErrors < source.size(); ++reportedCpuErrors) { errors.push_back(source[reportedCpuErrors]); }
    }
    bool hasSpace() {
        if (meshes.size() + textures.size() + materials.size() < 4096) { return true; }
        if (!limitReported) { errors.emplace_back("GPU asset cache limit exceeded (4096 paths)"); limitReported = true; }
        return false;
    }
    bool ensureSampler(ID3D11Device* device, std::size_t index, std::string& error) {
        if (samplers[index]) { return true; }
        D3D11_SAMPLER_DESC desc{};
        const D3D11_FILTER filters[] = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_FILTER_MIN_MAG_MIP_LINEAR};
        desc.Filter = filters[index / 2];
        desc.AddressU = desc.AddressV = desc.AddressW = index % 2 == 0 ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        const HRESULT hr = device->CreateSamplerState(&desc, samplers[index].GetAddressOf());
        if (FAILED(hr)) { error = "Cannot create texture sampler (HRESULT " + std::to_string(hr) + ")"; return false; }
        return true;
    }
    ID3D11SamplerState* materialSampler(ID3D11Device* device, const MaterialTextureSampling& sampling,
        std::size_t textureSampler, std::string& error) {
        if (sampling.useTextureSampler) {
            return ensureSampler(device, textureSampler, error) ? samplers[textureSampler].Get() : nullptr;
        }
        const auto wrapU = static_cast<std::size_t>(sampling.wrapU);
        const auto wrapV = static_cast<std::size_t>(sampling.wrapV);
        const std::size_t filter = (sampling.linearMin ? 1 : 0) | (sampling.linearMag ? 2 : 0) |
            (sampling.linearMip ? 4 : 0) | (sampling.mipmaps ? 8 : 0);
        auto& sampler = materialSamplers[(wrapU * 3 + wrapV) * 16 + filter];
        if (!sampler) {
            constexpr D3D11_TEXTURE_ADDRESS_MODE kAddress[] = {
                D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_MIRROR
            };
            D3D11_SAMPLER_DESC desc{};
            desc.Filter = D3D11_ENCODE_BASIC_FILTER(
                sampling.linearMin ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                sampling.linearMag ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                sampling.linearMip ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                D3D11_FILTER_REDUCTION_TYPE_STANDARD);
            desc.AddressU = kAddress[wrapU];
            desc.AddressV = kAddress[wrapV];
            desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.MaxAnisotropy = 1;
            desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            desc.MaxLOD = sampling.mipmaps ? D3D11_FLOAT32_MAX : 0.0f;
            const HRESULT hr = device->CreateSamplerState(&desc, sampler.GetAddressOf());
            if (FAILED(hr)) { error = "Cannot create material sampler (HRESULT " + std::to_string(hr) + ")"; }
        }
        return sampler.Get();
    }
    static void prepareEmbeddedTextures(const AssetReader& reader, std::string_view mesh, PreparedAssetImport& asset) {
        // A single mesh may contain many large images. Bound the extra decoded
        // data kept in a worker result; the rest use the existing upload path.
        constexpr std::size_t kMaxPreparedImageBytes = 96 * 1024 * 1024;
        std::size_t available = kMaxPreparedImageBytes;
        for (const auto& image : asset.mesh.embeddedImages) {
            PreparedEmbeddedTexture prepared;
            {
                std::string bytes;
                if (reader(cookedEmbeddedTexturePath(mesh, image.path), bytes, prepared.error) == AssetReadResult::Found) {
                    (void)decodeCookedTexture(bytes, prepared.data, prepared.error);
                }
            }
            if (prepared.data.width == 0 &&
                !importTexture(image.bytes, embeddedTextureSettings(asset.mesh, image.path), prepared.data, prepared.error)) {
                asset.embeddedTextures.push_back(std::move(prepared));
                continue;
            }
            const auto bytesOf = [](const auto& mip) { return mip.pixels.size() + mip.hdrPixels.size() * sizeof(float); };
            auto bytes = bytesOf(prepared.data);
            for (const auto& mip : prepared.data.mips) { bytes += bytesOf(mip); }
            if (bytes > available) { break; }
            available -= bytes;
            asset.embeddedTextures.push_back(std::move(prepared));
        }
    }
    bool createMeshAsset(ID3D11Device* device, ID3D11DeviceContext* context, const MeshData& data, MeshResource& resource,
        std::string& error, std::vector<std::string>& diagnostics, std::span<const PreparedEmbeddedTexture> prepared = {}) {
        // Light-only models have no geometry buffers to upload.
        const bool lightOnly = data.vertices.empty() && data.indices.empty() && data.nodeIndices.empty() && !data.lights.empty();
        if (!lightOnly && !createMesh(device, context, vertexPool, indexPool, data, resource, error,
            static_cast<bool>(thumbnailReader))) { return false; }
        resource.sourceMaterials = data.materials;
        resource.materialAssetPaths = data.materialAssetPaths;
        resource.dependencies = data.dependencies;
        if (!data.materialWarning.empty()) { diagnostics.push_back(data.materialWarning); }
        for (std::size_t i = 0; i < data.embeddedImages.size(); ++i) {
            const auto& image = data.embeddedImages[i];
            auto& texture = resource.embeddedTextures.try_emplace(image.path).first->second;
            TextureData decoded;
            std::string imageError;
            if (i < prepared.size()) {
                imageError = prepared[i].error;
            } else {
                auto settings = embeddedTextureSettings(data, image.path);
                if (thumbnailReader) { settings.maximumSize = kThumbnailTextureSize; }
                (void)importTexture(image.bytes, settings, decoded, imageError);
            }
            const auto& pixels = i < prepared.size() ? prepared[i].data : decoded;
            if (pixels.width == 0 || !createTexture(device, pixels, texture, imageError)) {
                diagnostics.push_back(image.path + ": " + imageError);
            }
        }
        return true;
    }
    void reportMeshDiagnostics(std::string_view path, const std::vector<std::string>& diagnostics) {
        for (const auto& diagnostic : diagnostics) { errors.push_back(std::string(path) + ": " + diagnostic); }
    }
    void loadMesh(ID3D11Device* device, ID3D11DeviceContext* context, std::string_view path) {
        if (meshes.contains(path) || !hasSpace()) { return; }
        auto& destination = meshes.try_emplace(std::string(path)).first->second;
        if (workerReader) { enqueue(path); return; }
        const auto* data = library.mesh(path);
        if (data == nullptr) { reportCpuErrors(); return; }
        MeshResource candidate;
        std::string error;
        std::vector<std::string> diagnostics;
        if (!createMeshAsset(device, context, *data, candidate, error, diagnostics)) {
            errors.push_back(std::string(path) + ": " + error);
            return;
        }
        destination = std::move(candidate);
        reportMeshDiagnostics(path, diagnostics);
    }
    void loadTexture(ID3D11Device* device, std::string_view path) {
        if (textures.contains(path) || !hasSpace()) { return; }
        auto& destination = textures.try_emplace(std::string(path)).first->second;
        if (workerReader) { enqueue(path); return; }
        TextureData preview;
        const TextureData* data = nullptr;
        std::string error;
        if (thumbnailReader) {
            AssetImportSettings settings;
            std::string bytes;
            const auto metadata = thumbnailReader(std::string(path) + ".satimport", bytes, error);
            if (metadata != AssetReadResult::Error &&
                (metadata != AssetReadResult::Found || parseImportSettings(bytes, settings, error)) &&
                thumbnailReader(path, bytes, error) == AssetReadResult::Found) {
                settings.maximumSize = std::min(settings.maximumSize, kThumbnailTextureSize);
                if (importTexture(bytes, settings, preview, error)) { data = &preview; }
            }
            if (!data) {
                if (error.empty()) { error = "Cannot read thumbnail texture"; }
                errors.push_back(std::string(path) + ": " + error);
                return;
            }
        } else {
            data = library.texture(path);
            if (!data) { reportCpuErrors(); return; }
        }
        TextureResource candidate;
        if (!createTexture(device, *data, candidate, error) || !ensureSampler(device, candidate.sampler, error)) {
            errors.push_back(std::string(path) + ": " + error); return;
        }
        destination = std::move(candidate);
    }
    bool ensureDefaults(ID3D11Device* device) {
        if (defaultsReady || defaultsFailed) { return defaultsReady; }
        constexpr unsigned char pixels[][4] = {
            {255, 255, 255, 255}, {128, 128, 255, 255}, {0, 0, 0, 255}, {255, 0, 255, 255}
        };
        static_assert(std::size(pixels) == std::tuple_size_v<decltype(defaults)>);
        std::string error;
        TextureData data;
        data.width = data.height = 1;
        for (std::size_t i = 0; i < defaults.size(); ++i) {
            data.pixels.assign(pixels[i], pixels[i] + 4);
            if (!createTexture(device, data, defaults[i], error)) {
                defaultsFailed = true;
                errors.push_back("Default material: " + error);
                return false;
            }
        }
        defaultsReady = true;
        return true;
    }
    ID3D11ShaderResourceView* colorView(ID3D11Device* device, TextureResource& texture) {
        if (!texture.view) { return nullptr; }
        if (!texture.srgb) {
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            texture.view->GetDesc(&desc);
            desc.Format = desc.Format == DXGI_FORMAT_BC7_UNORM ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            ComPtr<ID3D11Resource> resource;
            texture.view->GetResource(resource.GetAddressOf());
            if (FAILED(device->CreateShaderResourceView(resource.Get(), &desc, texture.srgb.GetAddressOf()))) {
                return nullptr;
            }
        }
        return texture.srgb.Get();
    }
    void createMaterial(ID3D11Device* device, std::string_view path, const MaterialData& data,
        MaterialResource& material, Textures* embedded = nullptr) {
        constexpr std::size_t kDefaultSampler = 4;
        ID3D11ShaderResourceView* views[] = {
            defaults[data.baseColorTexture.empty() ? 0 : kMissingBaseColor].view.Get(),
            defaults[0].view.Get(), defaults[1].view.Get(), defaults[2].view.Get(),
            defaults[0].view.Get(), defaults[0].view.Get(), defaults[0].view.Get(),
            defaults[0].view.Get(), defaults[0].view.Get(), defaults[1].view.Get()
        };
        const std::string* paths[] = {&data.baseColorTexture, &data.metallicRoughnessTexture,
            &data.normalTexture, &data.emissiveTexture, &data.occlusionTexture, &data.transmissionTexture, &data.thicknessTexture,
            &data.clearcoatTexture, &data.clearcoatRoughnessTexture, &data.clearcoatNormalTexture};
        ID3D11SamplerState* boundSamplers[std::size(paths)]{};
        std::array<std::size_t, std::size(paths)> samplerIndices;
        samplerIndices.fill(kDefaultSampler);
        bool texturesPending = false;
        bool hasEmissiveTexture = false;
        bool hasOcclusionTexture = false;
        bool hasClearcoatNormal = false;
        for (std::size_t i = 0; i < std::size(paths); ++i) {
            if (paths[i]->empty()) { continue; }
            TextureResource* texture = nullptr;
            if (embedded) {
                if (const auto found = embedded->find(*paths[i]); found != embedded->end()) { texture = &found->second; }
            }
            if (!texture) {
                material.textures.push_back(*paths[i]);
                loadTexture(device, *paths[i]);
                if (const auto found = textures.find(*paths[i]); found != textures.end()) { texture = &found->second; }
                if ((!texture || !texture->view) && scheduled.contains(*paths[i])) { texturesPending = true; }
            }
            if (!texture || !texture->view) { continue; }
            auto* view = i == 0 || i == 3 ? colorView(device, *texture) : texture->view.Get();
            if (!view) {
                errors.push_back(*paths[i] + ": cannot create sRGB texture view");
                continue;
            }
            views[i] = view;
            hasEmissiveTexture |= i == 3;
            hasOcclusionTexture |= i == 4;
            hasClearcoatNormal |= i == 9;
            samplerIndices[i] = texture->sampler;
        }
        std::string error;
        const bool transmissive = data.shader == MaterialShader::Lit && data.transmission > 0.0f &&
            data.ior > 0.0f && (data.metallic < 1.0f || !data.metallicRoughnessTexture.empty());
        const bool volume = transmissive && data.thickness > 0.0f;
        MaterialCb parameters{
            {data.baseColor[0], data.baseColor[1], data.baseColor[2], data.baseColor[3]},
            {data.emissive[0], data.emissive[1], data.emissive[2]}, data.normalScale,
            data.metallic, data.roughness, hasEmissiveTexture ? 1.0f : 0.0f,
            data.shader == MaterialShader::Unlit ? 1.0f : 0.0f,
            static_cast<float>(data.alphaMode), data.alphaCutoff, data.specularStrength,
            hasOcclusionTexture ? data.occlusionStrength : 0.0f, {},
            transmissive ? data.transmission : 0.0f, data.ior, data.thickness, data.attenuationDistance,
            {data.attenuationColor[0], data.attenuationColor[1], data.attenuationColor[2]}, data.transmissionMip, {},
            data.clearcoat, data.clearcoatRoughness, data.clearcoatNormalScale, hasClearcoatNormal ? 1.0f : 0.0f,
            data.transmissionRoughnessStrength, {}
        };
        for (std::size_t i = 0; i < std::size(paths); ++i) {
            const auto& sampling = data.textureSampling[i];
            boundSamplers[i] = materialSampler(device, sampling, samplerIndices[i], error);
            if (!boundSamplers[i]) {
                errors.push_back(std::string(path) + ": " + error);
                return;
            }
            const float c = std::cos(sampling.rotation);
            const float s = std::sin(sampling.rotation);
            auto& uv = i < std::size(parameters.uv) ? parameters.uv[i] : parameters.coatUv[i - std::size(parameters.uv)];
            uv = {
                {c * sampling.scale[0], -s * sampling.scale[1], s * sampling.scale[0], c * sampling.scale[1]},
                {sampling.offset[0], sampling.offset[1]}, sampling.texCoord,
                sampling.texCoord != 0 || sampling.rotation != 0.0f || sampling.scale[0] <= 0.0f || sampling.scale[1] <= 0.0f
            };
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(parameters);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = &parameters;
        if (FAILED(device->CreateBuffer(&desc, &initial, material.constants.GetAddressOf()))) {
            errors.push_back(std::string(path) + ": cannot create material constant buffer");
            return;
        }
        auto& gpu = material.view;
        gpu.texturesPending = texturesPending;
        gpu.alphaMode = data.alphaMode;
        gpu.shaderPath = data.shaderPath;
        gpu.constants = material.constants.Get();
        std::copy_n(views, std::size(gpu.views), gpu.views);
        std::copy_n(views + std::size(gpu.views), std::size(gpu.extraViews), gpu.extraViews);
        std::copy_n(boundSamplers, std::size(gpu.samplers), gpu.samplers);
        gpu.doubleSided = data.doubleSided && !volume;
        gpu.transmissive = transmissive;
        gpu.volume = volume;
    }
    void loadMaterial(ID3D11Device* device, std::string_view path) {
        if (materials.contains(path) || (!path.empty() && !hasSpace()) || !ensureDefaults(device)) { return; }
        auto& material = materials.try_emplace(std::string(path)).first->second;
        const MaterialData fallback;
        const auto* data = path.empty() ? &fallback : library.material(path);
        if (!data) { reportCpuErrors(); return; }
        createMaterial(device, path, *data, material);
    }
    void loadMeshMaterials(ID3D11Device* device, std::string_view path, MeshResource& mesh) {
        if (mesh.materialsReady || mesh.sourceMaterials.empty() || !ensureDefaults(device)) { return; }
        mesh.materials.resize(mesh.sourceMaterials.size());
        for (std::size_t i = 0; i < mesh.sourceMaterials.size(); ++i) {
            if (!mesh.materialAssetPaths.empty() && !mesh.materialAssetPaths[i].empty()) {
                createMaterial(device, mesh.materialAssetPaths[i], mesh.sourceMaterials[i], mesh.materials[i]);
            } else {
                createMaterial(device, path, mesh.sourceMaterials[i], mesh.materials[i], &mesh.embeddedTextures);
            }
        }
        mesh.view.texturesPending = false;
        for (auto& section : mesh.view.sections) {
            const auto index = section.range.materialIndex;
            if (index < mesh.materials.size() && mesh.materials[index].constants) {
                section.material = &mesh.materials[index].view;
                mesh.view.texturesPending |= section.material->texturesPending;
            }
        }
        mesh.materialsReady = true;
    }
    bool invalidateTextureMaterials(std::string_view changed) {
        const auto uses = [changed](const MaterialResource& material) {
            return std::ranges::find(material.textures, changed) != material.textures.end();
        };
        bool invalidated = std::erase_if(materials, [&](const auto& entry) { return uses(entry.second); }) != 0;
        for (auto& [path, mesh] : meshes) {
            if (!mesh.materialsReady || !std::ranges::any_of(mesh.materials, uses)) { continue; }
            for (auto& section : mesh.view.sections) { section.material = nullptr; }
            mesh.materials.clear();
            mesh.materialsReady = false;
            invalidated = true;
        }
        return invalidated;
    }
};
GpuAssetCache::GpuAssetCache() : m_impl(std::make_unique<Impl>()) {}
GpuAssetCache::~GpuAssetCache() = default;
void GpuAssetCache::setReader(AssetReader reader, bool asynchronous) {
    m_impl->requests.clear();
    m_impl->reloads.clear();
    m_impl->refinements.clear();
    m_impl->scheduled.clear();
    m_impl->failedDependencies.clear();
    for (auto& job : m_impl->inFlight) { job.discard = true; }
    m_impl->workerReader = asynchronous ? reader : AssetReader{};
    m_impl->materials.clear(); m_impl->defaults = {};
    m_impl->defaultsReady = m_impl->defaultsFailed = false;
    m_impl->meshes.clear();
    m_impl->vertexPool = {}; m_impl->indexPool = {};
    m_impl->textures.clear(); m_impl->samplers = {};
    m_impl->materialSamplers = {};
    m_impl->errors.clear(); m_impl->reportedCpuErrors = 0; m_impl->limitReported = false;
    m_impl->library.setReader(std::move(reader));
}
bool GpuAssetCache::pollUpload(ID3D11Device* device, bool allowReload) {
    auto& cache = *m_impl;
    bool published = false;
    if (!cache.inFlight.empty()) {
        const auto deadline = std::chrono::steady_clock::now() + Impl::kUploadBudget;
        for (auto job = cache.inFlight.begin(); job != cache.inFlight.end();) {
            if (job->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++job;
                continue;
            }
            if (!job->discard && !allowReload && cache.reloads.contains(job->path)) {
                cache.requests.push_back(job->path);
                job->discard = true;
            }
            if (!job->discard) {
                bool uploaded = false;
                bool refine = false;
                try {
                    auto result = job->result.get();
                    if (result.error.empty() && result.asset.isMesh) {
                        // Start external maps as soon as geometry decoding finishes,
                        // before the next scene prepare discovers its materials.
                        for (std::size_t i = 0; i < result.asset.mesh.materials.size(); ++i) {
                            const auto& mesh = result.asset.mesh;
                            const auto& material = mesh.materials[i];
                            const bool linked = i < mesh.materialAssetPaths.size() && !mesh.materialAssetPaths[i].empty();
                            for (const auto* path : {&material.baseColorTexture, &material.metallicRoughnessTexture,
                                &material.normalTexture, &material.emissiveTexture, &material.occlusionTexture,
                                &material.transmissionTexture, &material.thicknessTexture, &material.clearcoatTexture,
                                &material.clearcoatRoughnessTexture, &material.clearcoatNormalTexture}) {
                                if (path->empty()) { continue; }
                                if (!linked && std::ranges::find(mesh.embeddedImages, *path, &GltfImage::path) !=
                                    mesh.embeddedImages.end()) { continue; }
                                cache.loadTexture(device, *path);
                            }
                        }
                    }
                    using Clock = RenderBenchmark::Clock;
                    const auto start = result.measured ? Clock::now() : Clock::time_point{};
                    uploaded = result.error.empty() && commitImport(device, result.asset, {}, result.error);
                    if (result.measured) {
                        RenderBenchmark::recordTexture(result.preview, uploaded, result.readTime, result.decodeTime,
                            Clock::now() - start);
                    }
                    if (uploaded) {
                        published = true;
                        refine = result.preview;
                        cache.failedDependencies.erase(job->path);
                    } else {
                        cache.failedDependencies[job->path] = std::move(result.dependencies);
                        cache.reportImportError(job->path, result.error);
                    }
                } catch (const std::exception& error) {
                    cache.reportImportError(job->path, error.what());
                }
                cache.scheduled.erase(job->path);
                cache.reloads.erase(job->path);
                cache.refinements.erase(job->path);
                if (!uploaded) {
                    const auto texture = cache.textures.find(job->path);
                    if (texture != cache.textures.end() && !texture->second.view) {
                        // Settle materials waiting on a failed first load so they can use their error fallback.
                        published |= cache.invalidateTextureMaterials(job->path);
                    }
                }
                if (refine) {
                    cache.enqueue(job->path);
                    cache.refinements.insert(job->path);
                }
            }
            job = cache.inFlight.erase(job);
            // An individual upload can't be split. Stop before the next one
            // once the frame's budget has been spent.
            if (std::chrono::steady_clock::now() >= deadline) { break; }
        }
    }
    cache.startDecodes(allowReload);
    return published;
}

void GpuAssetCache::refresh(ID3D11Device* device, const std::unordered_set<std::string>& paths) {
    auto& cache = *m_impl;
    if (!device || !cache.workerReader) { return; }
    const auto changed = [&](const std::string& path) {
        return paths.contains(path) || paths.contains(path + ".satimport");
    };
    const auto dependenciesChanged = [&](const auto& dependencies) {
        return std::ranges::any_of(dependencies, changed);
    };
    const auto schedule = [&](const std::string& path) {
        cache.cancel(path);
        cache.enqueue(path);
        cache.reloads.insert(path);
    };
    for (const auto& job : cache.inFlight) {
        if (!job.discard && (changed(job.path) || isGltfAssetPath(job.path))) {
            schedule(job.path);
        }
    }
    for (const auto& [path, mesh] : cache.meshes) {
        const auto failed = cache.failedDependencies.find(path);
        if (changed(path) || dependenciesChanged(mesh.dependencies) ||
            (failed != cache.failedDependencies.end() && dependenciesChanged(failed->second))) {
            schedule(path);
        }
    }
    for (const auto& [path, texture] : cache.textures) {
        if (changed(path)) { schedule(path); }
    }
    for (const auto& path : paths) {
        const auto material = cache.materials.find(path);
        if (material == cache.materials.end()) { continue; }
        MaterialData data;
        std::string bytes, error;
        if (cache.workerReader(path, bytes, error) != AssetReadResult::Found || !decodeMaterial(bytes, data, error)) {
            cache.reportImportError(path, error.empty() ? "Cannot read material" : error);
            continue;
        }
        MaterialResource replacement;
        cache.createMaterial(device, path, data, replacement);
        if (replacement.constants) {
            material->second = std::move(replacement);
            cache.library.replaceMaterial(path, std::move(data));
            cache.reportedCpuErrors = cache.library.errors().size();
            cache.reportImportError(path, {});
        }
    }
}

void GpuAssetCache::prepareThumbnail(ID3D11Device* device, AssetReader reader, std::string_view path) {
    setReader(reader);
    auto& cache = *m_impl;
    cache.thumbnailReader = std::move(reader);
    cache.loadMaterial(device, {});
    if (isMeshAssetPath(path)) {
        cache.loadMesh(device, nullptr, path);
        const auto found = cache.meshes.find(path);
        if (found != cache.meshes.end()) { cache.loadMeshMaterials(device, path, found->second); }
    } else {
        cache.loadMaterial(device, path);
    }
    // The prepared pass crosses to the render thread without retaining the reader
    // (which borrows the worker's job) or the full decoded model.
    cache.thumbnailReader = {};
    cache.library.setReader({});
    cache.reportedCpuErrors = 0;
}

bool GpuAssetCache::prepareResidentThumbnail(ID3D11Device* device, std::string_view path,
    std::vector<std::string>& dependencies) {
    auto& cache = *m_impl;
    const auto found = cache.meshes.find(path);
    if (found == cache.meshes.end() || !found->second.view.valid() || cache.scheduled.contains(path)) {
        return false;
    }
    auto& mesh = found->second;
    cache.loadMaterial(device, {});
    cache.loadMeshMaterials(device, path, mesh);
    if (mesh.view.texturesPending || !material({}, false)) { return false; }

    dependencies = mesh.dependencies;
    for (const auto& material : mesh.materials) {
        dependencies.insert(dependencies.end(), material.textures.begin(), material.textures.end());
    }
    return std::ranges::none_of(dependencies, [&](const std::string& dependency) {
        return cache.scheduled.contains(dependency);
    });
}
void GpuAssetCache::prepare(ID3D11Device* device, const SceneDrawList& drawList, ID3D11DeviceContext* context) {
    if (device == nullptr) { return; }
    if (!drawList.environmentPath.empty()) {
        m_impl->loadTexture(device, drawList.environmentPath);
        const auto found = m_impl->textures.find(drawList.environmentPath);
        if (found != m_impl->textures.end()) { (void)m_impl->colorView(device, found->second); }
    }
    m_impl->loadMaterial(device, {});
    std::unordered_set<std::string_view> meshes, embedded, materials;
    const auto prepare = [&](std::string_view mesh, std::string_view material) {
        if (!mesh.empty()) {
            if (meshes.insert(mesh).second) { m_impl->loadMesh(device, context, mesh); }
            if (material.empty() && embedded.insert(mesh).second) {
                const auto found = m_impl->meshes.find(mesh);
                if (found != m_impl->meshes.end()) { m_impl->loadMeshMaterials(device, mesh, found->second); }
            }
        }
        if (!material.empty() && materials.insert(material).second) { m_impl->loadMaterial(device, material); }
    };
    if (drawList.scene) {
        for (const auto& entity : drawList.scene->entities()) {
            if (entity.hasMesh) { prepare(entity.mesh.meshPath, entity.mesh.materialPath); }
        }
    } else if (drawList.meshes) {
        for (int i = 0; i < drawList.meshCount; ++i) {
            const auto& mesh = drawList.meshes[i];
            prepare(mesh.meshPath, mesh.materialPath);
        }
    }
}
ID3D11ShaderResourceView* GpuAssetCache::environment(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found == m_impl->textures.end() ? nullptr : found->second.srgb.Get();
}

bool GpuAssetCache::reimport(ID3D11Device* device, std::string_view path, const AssetImportSettings* settings,
    const AssetImportWriter& writer, std::string& error) {
    if (!device) { error = "No graphics device for asset import"; return false; }
    m_impl->reportCpuErrors();
    const bool exists = isMeshAssetPath(path) ? m_impl->meshes.contains(path) : m_impl->textures.contains(path);
    if (!exists && !m_impl->hasSpace()) { error = "GPU asset cache limit exceeded"; return false; }
    PreparedAssetImport prepared;
    if (!m_impl->library.stage(path, settings, prepared, error)) { return false; }
    if (!publishPrepared(device, prepared, exists, writer, error)) { return false; }
    m_impl->cancel(path);
    return true;
}

bool GpuAssetCache::publishImport(ID3D11Device* device, PreparedAssetImport&& prepared,
    const AssetImportWriter& writer, std::string& error) {
    const auto path = prepared.path;
    if (!commitImport(device, prepared, writer, error)) { return false; }
    m_impl->cancel(path);
    return true;
}

bool GpuAssetCache::commitImport(ID3D11Device* device, PreparedAssetImport& prepared,
    const AssetImportWriter& writer, std::string& error) {
    if (!device) { error = "No graphics device for asset import"; return false; }
    m_impl->reportCpuErrors();
    const bool exists = prepared.isMesh ? m_impl->meshes.contains(prepared.path) : m_impl->textures.contains(prepared.path);
    if (!exists && !m_impl->hasSpace()) { error = "GPU asset cache limit exceeded"; return false; }
    if (!m_impl->library.reserveImport(prepared, error)) { return false; }
    return publishPrepared(device, prepared, exists, writer, error);
}

bool GpuAssetCache::publishPrepared(ID3D11Device* device, PreparedAssetImport& prepared, bool exists,
    const AssetImportWriter& writer, std::string& error) {
    const std::string& path = prepared.path;
    const auto cancel = [&] {
        if (!exists) {
            if (prepared.isMesh) { m_impl->meshes.erase(path); }
            else { m_impl->textures.erase(path); }
        }
        m_impl->library.discard(prepared);
        return false;
    };
    MeshResource mesh;
    TextureResource texture;
    std::vector<std::string> diagnostics;
    if (prepared.isMesh) {
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(context.GetAddressOf());
        if (!m_impl->createMeshAsset(device, context.Get(), prepared.mesh, mesh, error, diagnostics, prepared.embeddedTextures)) {
            return cancel();
        }
        m_impl->meshes.try_emplace(prepared.path);
    } else {
        if (!createTexture(device, prepared.texture, texture, error) || !m_impl->ensureSampler(device, texture.sampler, error)) { return cancel(); }
        m_impl->textures.try_emplace(prepared.path);
    }
    // All decoding, GPU allocations and map growth precede persistence. A failed
    // writer leaves active CPU/GPU resources untouched. Commit only moves data.
    if (writer && !writer(prepared.settings, error)) { return cancel(); }
    if (prepared.isMesh) { m_impl->meshes.find(path)->second = std::move(mesh); }
    else {
        m_impl->invalidateTextureMaterials(path);
        m_impl->textures.find(path)->second = std::move(texture);
    }
    std::erase_if(m_impl->errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
    m_impl->reportMeshDiagnostics(path, diagnostics);
    m_impl->library.commit(std::move(prepared));
    m_impl->reportedCpuErrors = m_impl->library.errors().size();
    return true;
}
void GpuAssetCache::invalidateAsset(std::string_view path) {
    m_impl->cancel(path);
    std::erase_if(m_impl->meshes, [&](const auto& entry) {
        const auto& dependencies = entry.second.dependencies;
        if (entry.first != path && std::ranges::find(dependencies, path) == dependencies.end()) { return false; }
        m_impl->cancel(entry.first);
        return true;
    });
    if (const auto found = m_impl->textures.find(path); found != m_impl->textures.end()) {
        m_impl->invalidateTextureMaterials(path);
        m_impl->textures.erase(found);
    }
    m_impl->library.invalidateAsset(path);
    m_impl->reportedCpuErrors = m_impl->library.errors().size();
    if (const auto found = m_impl->materials.find(path); found != m_impl->materials.end()) {
        m_impl->materials.erase(found);
    }
    std::erase_if(m_impl->errors, [path](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
}
void GpuAssetCache::invalidateMaterial(std::string_view path) {
    if (const auto found = m_impl->materials.find(path); found != m_impl->materials.end()) {
        m_impl->materials.erase(found);
    }
    m_impl->library.invalidateMaterial(path);
    m_impl->reportedCpuErrors = m_impl->library.errors().size();
    std::erase_if(m_impl->errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
}
const GpuMaterial* GpuAssetCache::material(std::string_view path, bool fallback) const {
    auto found = m_impl->materials.find(path);
    if (fallback && (found == m_impl->materials.end() || !found->second.constants)) { found = m_impl->materials.find(""); }
    return found != m_impl->materials.end() && found->second.constants ? &found->second.view : nullptr;
}
const MeshData* GpuAssetCache::decodedMesh(std::string_view path) {
    return m_impl->workerReader ? m_impl->library.cachedMesh(path) : m_impl->library.mesh(path);
}

const GpuMesh* GpuAssetCache::mesh(std::string_view path) const {
    const auto found = m_impl->meshes.find(path);
    return found != m_impl->meshes.end() && found->second.view.valid() ? &found->second.view : nullptr;
}
ID3D11ShaderResourceView* GpuAssetCache::texture(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found != m_impl->textures.end() ? found->second.view.Get() : nullptr;
}
ID3D11SamplerState* GpuAssetCache::sampler(std::string_view path) const {
    const auto found = m_impl->textures.find(path);
    return found != m_impl->textures.end() && found->second.view ? m_impl->samplers[found->second.sampler].Get() : nullptr;
}
bool GpuAssetCache::bounds(std::string_view path, Vec3& minimum, Vec3& maximum, std::uint32_t meshNode) {
    const auto loaded = m_impl->meshes.find(path);
    if (loaded != m_impl->meshes.end() && loaded->second.view.valid()) {
        if (meshNode == 0) {
            minimum = loaded->second.minimum;
            maximum = loaded->second.maximum;
            return true;
        }
        const auto* node = loaded->second.view.node(meshNode);
        if (!node) { return false; }
        minimum = node->minimum - node->origin;
        maximum = node->maximum - node->origin;
        return true;
    }
    const auto* mesh = decodedMesh(path);
    m_impl->reportCpuErrors();
    if (!mesh || mesh->vertices.empty()) { return false; }
    if (meshNode != 0) {
        const auto node = std::ranges::find(mesh->nodes, meshNode, &MeshNode::id);
        if (node == mesh->nodes.end() || node->sectionCount == 0) { return false; }
        minimum = {node->minimum[0] - node->origin[0], node->minimum[1] - node->origin[1], node->minimum[2] - node->origin[2]};
        maximum = {node->maximum[0] - node->origin[0], node->maximum[1] - node->origin[1], node->maximum[2] - node->origin[2]};
        return true;
    }
    minimum = {mesh->minimum[0], mesh->minimum[1], mesh->minimum[2]};
    maximum = {mesh->maximum[0], mesh->maximum[1], mesh->maximum[2]};
    return true;
}
std::string GpuAssetCache::info(std::string_view path) {
    std::string result;
    if (isMeshAssetPath(path)) {
        if (const auto* data = m_impl->library.mesh(path)) {
            result = std::to_string(data->vertices.size()) + " vertices, " + std::to_string(data->indices.size() / 3) + " triangles";
        }
    } else if (isTextureAssetPath(path)) {
        if (const auto* data = m_impl->library.texture(path)) {
            result = std::to_string(data->width) + " x " + std::to_string(data->height) + ", " + std::to_string(data->mips.size() + 1) + " mip levels";
        }
    }
    m_impl->reportCpuErrors();
    return result;
}
const std::vector<std::string>& GpuAssetCache::errors() const { return m_impl->errors; }
}
