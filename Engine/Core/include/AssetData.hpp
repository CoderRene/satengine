#pragma once

#include "AssetImportSettings.hpp"
#include "Math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <iosfwd>
#include <span>
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
[[nodiscard]] bool isFbxAssetPath(std::string_view path);
[[nodiscard]] bool isSceneModelAssetPath(std::string_view path);
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
    float u1 = 0.0f, v1 = 0.0f;
    float color[4] = {1, 1, 1, 1};
};
enum class MaterialShader { Lit, Unlit, Custom };

enum class AlphaMode { Opaque, Mask, Blend };

enum class MaterialWrap { Repeat, Clamp, Mirror };

struct MaterialTextureSampling {
    // glTF transform: offset + rotation * (scale * UV), with rotation in radians.
    float offset[2] = {};
    float scale[2] = {1, 1};
    float rotation = 0.0f;
    unsigned texCoord = 0;
    MaterialWrap wrapU = MaterialWrap::Repeat;
    MaterialWrap wrapV = MaterialWrap::Repeat;
    bool useTextureSampler = true;
    bool linearMin = true;
    bool linearMag = true;
    bool linearMip = true;
    bool mipmaps = true;

    bool operator==(const MaterialTextureSampling&) const = default;
};

// A 16384-pixel D3D11 texture reaches one texel on its longest axis at mip 14.
inline constexpr float kMaxTransmissionMip = 14.0f;

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
    bool doubleSided = false;
    float transmission = 0.0f;
    float transmissionMip = 1.0f; // Adds this value minus one to the roughness-driven mip level.
    float transmissionRoughnessStrength = 1.0f;
    float ior = 1.5f;
    float thickness = 0.0f; // Mesh-space distance.
    float attenuationDistance = 0.0f; // World-space distance. Zero means unlimited.
    float attenuationColor[3] = {1, 1, 1};
    std::string transmissionTexture;
    std::string thicknessTexture;
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.0f;
    float clearcoatNormalScale = 1.0f;
    std::string clearcoatTexture;
    std::string clearcoatRoughnessTexture;
    std::string clearcoatNormalTexture;
    // Base color, metallic-roughness, normal, emissive, occlusion, transmission, thickness,
    // clearcoat, clearcoat roughness, clearcoat normal.
    std::array<MaterialTextureSampling, 10> textureSampling{};

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
    Vec3 thicknessScale{1, 1, 1}; // Scale baked into the vertices during import.
};
struct MeshNode {
    std::string name;
    std::uint32_t id = 0; // Stable one-based source node identifier.
    std::uint32_t parent = 0;
    std::uint32_t firstSection = 0;
    std::uint32_t sectionCount = 0;
    float origin[3]{};
    float minimum[3]{};
    float maximum[3]{};
};
inline constexpr std::size_t kMaxAnimationJoints = 256;
inline constexpr std::size_t kMaxAnimationKeys = 1000000;
struct AnimationPose {
    Vec3 translation{};
    float rotation[4] = {0, 0, 0, 1}; // Quaternion XYZW, left-handed.
    Vec3 scale{1, 1, 1};
};
struct AnimationJoint {
    std::string name;
    int parent = -1;
    AnimationPose restTRS;
    Mat4 restLocal; // Preserves unanimated matrix nodes, including shear.
    Mat4 inverseBind;
};
enum class AnimationPath : std::uint8_t { Translation, Rotation, Scale };
enum class AnimationInterpolation : std::uint8_t { Step, Linear, CubicSpline };
struct AnimationChannel {
    std::uint16_t joint = 0;
    AnimationPath path = AnimationPath::Translation;
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<float> times;
    std::vector<float> values;
};
struct AnimationClip {
    std::string name;
    float duration = 0;
    std::vector<AnimationChannel> channels;
};
struct AnimationData {
    std::vector<AnimationJoint> joints;
    std::vector<AnimationClip> clips;
};
struct MeshSkinVertex {
    std::uint8_t joints[4]{};
    std::uint8_t weights[4]{};
};
static_assert(sizeof(MeshSkinVertex) == 8);
struct AnimationBounds {
    Vec3 minimum{}, maximum{};
    bool valid = false;
};
// Expands an AABB by a joint's transformed bind-space bounds.
void includeSkinnedBounds(AnimationBounds& bounds, const AnimationBounds& bind, const Mat4& palette);

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<MeshSkinVertex> skinVertices;
    std::vector<AnimationBounds> jointBounds;
    std::vector<Mat4> restPalette;
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
    std::vector<std::string> materialNames; // Source order, when available.
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
struct ObjMaterial {
    std::string name;
    MaterialData material;
};
// Reads definitions in OBJ library order. Repeated definitions replace earlier ones.
[[nodiscard]] bool decodeObjMaterials(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::vector<ObjMaterial>& materials, std::string& error);
// Produces unique sibling SATMAT paths in source order.
[[nodiscard]] std::vector<std::string> meshMaterialPaths(std::string_view path, std::span<const std::string> names);
// Static glTF 2.0 scene flattened into a single left-handed mesh. The reader
// resolves companion files. Decoder outputs are replaced only on success.
[[nodiscard]] bool decodeGltf(std::string_view bytes, std::string_view path, const AssetReader& reader,
    MeshData& mesh, std::string& error);
[[nodiscard]] bool decodeGltfAnimation(std::string_view bytes, std::string_view path,
    const AssetReader& reader, AnimationData& animation, std::string& error);
// Lists material names and optional texture labels in source index order without reading companion files.
[[nodiscard]] bool gltfMaterialNames(std::string_view bytes, std::vector<std::string>& names, std::string& error,
    std::vector<std::string>* textures = nullptr);
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
