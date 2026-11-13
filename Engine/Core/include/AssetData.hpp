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
[[nodiscard]] bool isAudioAssetPath(std::string_view path);
inline constexpr std::size_t kMaxAssetBytes = 64 * 1024 * 1024;

// Left-handed positions, +Y up, +Z forward. UV origin is top-left.
struct MeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    float minimum[3] = {};
    float maximum[3] = {};
};
struct TextureMip {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};
struct TextureData {
    int width = 0;
    int height = 0;
    // Row-major RGBA8, top row first. Current unlit renderer preserves encoded color values.
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
