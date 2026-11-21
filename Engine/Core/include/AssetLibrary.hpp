#pragma once

#include "AssetData.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sat {
// Persistence callback must not reenter or mutate the asset library/renderer.
using AssetImportWriter = std::function<bool(const AssetImportSettings&, std::string&)>;
[[nodiscard]] AssetReader fileAssetReader(const std::filesystem::path& projectRoot);
[[nodiscard]] AssetReader fileShaderReader(const std::filesystem::path& executableRoot);
// Reports dimensions after applying validated import settings without reading pixel data.
[[nodiscard]] bool inspectTextureFile(const std::filesystem::path& root, std::string_view path,
    const AssetImportSettings& settings, TextureInfo& info, std::string& error);

struct PreparedEmbeddedTexture {
    TextureData data;
    std::string error;
};

struct PreparedAssetImport {
    std::string path;
    AssetImportSettings settings;
    MeshData mesh;
    TextureData texture;
    std::vector<PreparedEmbeddedTexture> embeddedTextures;
    bool isMesh = false;
    bool reservedNew = false;
};
// Decodes without touching a cache. The reader must be safe to call on the worker.
[[nodiscard]] bool decodeAssetImport(const AssetReader& reader, std::string_view path,
    const AssetImportSettings* overrides, PreparedAssetImport& prepared, std::string& error);
// Window-thread only. Mesh and texture pointers can expire on any mesh/texture
// cache miss or commit. Material pointers expire when invalidated. All data
// expires on clear/setReader/destruction. Finish a stage with commit or discard
// before another staging operation or cache reset.
class AssetLibrary {
public:
    AssetLibrary() = default;
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;
    AssetLibrary(AssetLibrary&&) = delete;
    AssetLibrary& operator=(AssetLibrary&&) = delete;

    void setReader(AssetReader reader);
    void clear();
    void invalidateMaterial(std::string_view path);
    void replaceMaterial(std::string_view path, MaterialData&& data);
    void invalidateAsset(std::string_view path);
    [[nodiscard]] const MaterialData* material(std::string_view path);
    [[nodiscard]] const MeshData* mesh(std::string_view path);
    [[nodiscard]] const MeshData* cachedMesh(std::string_view path);
    [[nodiscard]] const TextureData* texture(std::string_view path);
    // Null overrides reads saved metadata, with missing metadata using defaults.
    // Reserves the cache slot before returning. Only commit may evict decoded
    // data, and it performs no allocations.
    [[nodiscard]] bool stage(std::string_view path, const AssetImportSettings* overrides,
        PreparedAssetImport& prepared, std::string& error);
    // Reserves a decoded result on the owning thread for commit or discard.
    [[nodiscard]] bool reserveImport(PreparedAssetImport& prepared, std::string& error);
    void commit(PreparedAssetImport&& prepared);
    void discard(PreparedAssetImport& prepared);
    [[nodiscard]] const std::vector<std::string>& errors() const { return m_errors; }
private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };
    template<typename T> struct Entry { T data; bool valid = false; };
    using Recency = std::list<std::string_view>;
    template<typename T> struct DecodedEntry : Entry<T> { Recency::iterator recency; };
    template<typename T> using Cache = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

    template<typename T> void reserveDecoded(Cache<DecodedEntry<T>>& cache, const PreparedAssetImport& prepared);
    template<typename T> void discardDecoded(Cache<DecodedEntry<T>>& cache, const PreparedAssetImport& prepared);
    template<typename T> void commitDecoded(DecodedEntry<T>& entry, T&& data);
    void evictOldest();
    [[nodiscard]] bool hasSpaceFor(std::string_view path, bool isMesh) const;

    std::size_t m_cachedBytes = 0;
    bool m_limitReported = false;
    AssetReader m_reader;
    Cache<DecodedEntry<MeshData>> m_meshes;
    Cache<DecodedEntry<TextureData>> m_textures;
    Cache<Entry<MaterialData>> m_materials;
    Recency m_recency; // Most recent first. Views borrow keys from the decoded caches.
    std::vector<std::string> m_errors;
};
}
