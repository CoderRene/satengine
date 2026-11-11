#pragma once

#include "AssetData.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sat {
enum class AssetReadResult { Found, Missing, Error };
// Window-thread cache-miss callback; copies bytes out. Captured sources must live
// through use. Missing is distinct from unreadable/invalid/empty existing files.
using AssetReader = std::function<AssetReadResult(std::string_view, std::string&, std::string&)>;
// Persistence callback must not reenter or mutate the asset library/renderer.
using AssetImportWriter = std::function<bool(const AssetImportSettings&, std::string&)>;
[[nodiscard]] AssetReader fileAssetReader(const std::filesystem::path& projectRoot);

struct PreparedAssetImport {
    std::string path;
    AssetImportSettings settings;
    MeshData mesh;
    TextureData texture;
    bool isMesh = false;
    bool reservedNew = false;
};
// Window-thread only. Pointers survive growth; replaced path data expires on
// commit, all data expires on clear/setReader/destruction. Never retain a stage
// across another staging operation, cache reset or commit.
class AssetLibrary {
public:
    void setReader(AssetReader reader);
    void clear();
    [[nodiscard]] const MeshData* mesh(std::string_view path);
    [[nodiscard]] const TextureData* texture(std::string_view path);
    // Null overrides reads saved metadata, with missing metadata using defaults.
    // Reserves the cache slot before returning; commit performs no allocations.
    [[nodiscard]] bool stage(std::string_view path, const AssetImportSettings* overrides,
        PreparedAssetImport& prepared, std::string& error);
    void commit(PreparedAssetImport&& prepared);
    void discard(PreparedAssetImport& prepared);
    [[nodiscard]] const std::vector<std::string>& errors() const { return m_errors; }
private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };
    template<typename T> struct Entry { T data; bool valid = false; };
    template<typename T> using Cache = std::unordered_map<std::string, Entry<T>, StringHash, std::equal_to<>>;
    std::size_t m_cachedBytes = 0;
    bool m_limitReported = false;
    AssetReader m_reader;
    Cache<MeshData> m_meshes;
    Cache<TextureData> m_textures;
    std::vector<std::string> m_errors;
};
}
