#pragma once

#include "AssetImportSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

enum class AssetReadResult { Found, Missing, Error };
// Window-thread cache-miss callback; copies bytes out. Captured sources must live
// through use. Missing is distinct from unreadable/invalid/empty existing files.
using AssetReader = std::function<AssetReadResult(std::string_view, std::string&, std::string&)>;

// Project-relative UTF-8 paths under Assets/, with '/' separators and no traversal.
[[nodiscard]] bool isValidAssetPath(std::string_view path);
[[nodiscard]] bool isMeshAssetPath(std::string_view path);
[[nodiscard]] bool isGltfAssetPath(std::string_view path);
[[nodiscard]] bool isTextureAssetPath(std::string_view path);
[[nodiscard]] bool isShaderAssetPath(std::string_view path);
[[nodiscard]] bool isMaterialAssetPath(std::string_view path);
[[nodiscard]] bool isAudioAssetPath(std::string_view path);
inline constexpr std::size_t kMaxAssetBytes = 64 * 1024 * 1024;

// Left-handed positions, +Y up, +Z forward. UV origin is top-left.
struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = -1.0f;
    float u = 0.0f;
    float v = 0.0f;
    float tx = 1.0f, ty = 0.0f, tz = 0.0f, tw = 1.0f;
};
enum class MaterialShader { Lit, Unlit, Custom };

enum class AlphaMode { Opaque, Mask, Blend };

struct MaterialData {
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    MaterialShader shader = MaterialShader::Lit;
    std::string shaderPath;
    float baseColor[4] = {1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specularStrength = 1.0f;
    float emissive[3] = {};
    float normalScale = 1.0f;
    std::string baseColorTexture;
    std::string metallicRoughnessTexture;
    std::string normalTexture;
    std::string emissiveTexture;
    std::string occlusionTexture;
    float occlusionStrength = 1.0f;

    bool operator==(const MaterialData&) const = default;
};

struct GltfImage {
    std::string path;
    std::string bytes;
};
inline constexpr auto kNoMeshMaterial = (std::numeric_limits<std::uint32_t>::max)();
struct MeshSection {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = kNoMeshMaterial; // Index into MeshData::materials.
};
struct MeshNode {
    std::string name;
    std::uint32_t id = 0; // One-based index into the source glTF node array.
    std::uint32_t parent = 0;
    std::uint32_t firstSection = 0;
    std::uint32_t sectionCount = 0;
    float origin[3]{};
    float minimum[3]{};
    float maximum[3]{};
};
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    float minimum[3] = {};
    float maximum[3] = {};
    // Empty sections draw the full mesh with the entity material.
    std::vector<MeshSection> sections;
    // Parent-first traversal. Nodes reference original ranges when the main mesh is merged.
    std::vector<MeshNode> nodes;
    std::vector<MeshSection> nodeSections;
    std::vector<std::uint32_t> nodeIndices;
    bool mergeNodeDraws = false;
    std::vector<MaterialData> materials;
    std::vector<GltfImage> embeddedImages;
    std::string materialWarning;
    std::vector<std::string> dependencies; // Files read during mesh import.
};

[[nodiscard]] bool decodeMaterial(std::string_view bytes, MaterialData& material, std::string& error);
[[nodiscard]] std::string encodeMaterial(const MaterialData& material);

struct TextureMip {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};
struct TextureData {
    int width = 0;
    int height = 0;
    // Row-major RGBA8, top row first.
    std::vector<unsigned char> pixels;
    std::vector<TextureMip> mips; // Lower levels only; pixels above owns level zero.
    TextureFilter filter = TextureFilter::Bilinear;
    TextureWrap wrap = TextureWrap::Repeat;
};

struct TextureInfo {
    int width = 0;
    int height = 0;
    int mipLevels = 1;
};

// Bounded static OBJ subset. Reflects Z and reverses winding from right-handed
// source coordinates; flips V. Fan-triangulates convex polygon faces.
// Failures preserve the destination and provide line-number context.
[[nodiscard]] bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error);
// Importer overload applies source conventions directly, avoiding lossy UV undo.
[[nodiscard]] bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error, bool rightHanded, bool flipV);
// Static glTF 2.0 scene flattened into a single left-handed mesh. The reader
// resolves companion files. Decoder outputs are replaced only on success.
[[nodiscard]] bool decodeGltf(std::string_view bytes, std::string_view path, const AssetReader& reader,
    MeshData& mesh, std::string& error);
// Lists names in source index order without reading companion files.
[[nodiscard]] bool gltfMaterialNames(std::string_view bytes, std::vector<std::string>& names, std::string& error);
[[nodiscard]] bool decodeGltfMaterial(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::size_t materialIndex, MaterialData& material, std::vector<GltfImage>& embedded, std::string& error);
// Decodes all materials in source order and returns each shared embedded image once.
[[nodiscard]] bool decodeGltfMaterials(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::vector<MaterialData>& materials, std::vector<GltfImage>& embedded, std::string& error);
// PNG/JPEG decoded to RGBA8; bounded to 4096x4096 and 64 MiB decoded data.
[[nodiscard]] bool decodeTexture(std::string_view bytes, TextureData& texture, std::string& error);
// Reads the PNG/JPEG header from a seekable stream positioned at the start.
[[nodiscard]] bool readTextureDimensions(std::istream& stream, int& width, int& height, std::string& error);

} // namespace sat
