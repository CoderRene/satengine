#pragma once

#include "AssetImportSettings.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

// Project-relative UTF-8 paths under Assets/, with '/' separators and no traversal.
[[nodiscard]] bool isValidAssetPath(std::string_view path);
[[nodiscard]] bool isMeshAssetPath(std::string_view path);
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
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    float minimum[3] = {};
    float maximum[3] = {};
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

    bool operator==(const MaterialData&) const = default;
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

// Bounded static OBJ subset. Reflects Z and reverses winding from right-handed
// source coordinates; flips V. Fan-triangulates convex polygon faces.
// Failures preserve the destination and provide line-number context.
[[nodiscard]] bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error);
// Importer overload applies source conventions directly, avoiding lossy UV undo.
[[nodiscard]] bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error, bool rightHanded, bool flipV);
// PNG/JPEG decoded to RGBA8; bounded to 4096x4096 and 64 MiB decoded data.
[[nodiscard]] bool decodeTexture(std::string_view bytes, TextureData& texture, std::string& error);

} // namespace sat
