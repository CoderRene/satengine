#include "AssetData.hpp"

#include "Math.hpp"
#include "AnimationAsset.hpp"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sat {
namespace {
constexpr std::size_t kMaxVertices = 1000000;
constexpr std::size_t kMaxIndices = 3000000;

bool extensionIs(std::string_view path, std::string_view extension) {
    if (path.size() < extension.size()) { return false; }
    path.remove_prefix(path.size() - extension.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        char ch = path[i];
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch + ('a' - 'A')); }
        if (ch != extension[i]) { return false; }
    }
    return true;
}
std::string_view token(std::string_view& line) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) { line = {}; return {}; }
    line.remove_prefix(first);
    const auto end = line.find_first_of(" \t\r");
    const auto result = line.substr(0, end);
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end);
    return result;
}
Vec3 unitVector(const Vec3& vector) {
    const float magnitude = std::max({std::abs(vector.x), std::abs(vector.y), std::abs(vector.z)});
    if (!std::isfinite(magnitude) || magnitude < std::numeric_limits<float>::min()) { return {0, 0, 1}; }
    return normalize(vector * (1.0f / magnitude));
}
bool number(std::string_view value, float& result) {
    if (value.starts_with('+')) { value.remove_prefix(1); }
    if (value.empty()) { return false; }
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return !value.empty() && parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && std::isfinite(result);
}
Vec3 positionOf(const MeshVertex& vertex) {
    return {vertex.x, vertex.y, vertex.z};
}
void accumulateTangent(MeshData& mesh, std::vector<Vec3>& bitangents,
    std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
    const auto& a = mesh.vertices[ia];
    const auto& b = mesh.vertices[ib];
    const auto& c = mesh.vertices[ic];
    const Vec3 ab = positionOf(b) - positionOf(a);
    const Vec3 ac = positionOf(c) - positionOf(a);
    const float det = (b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v);
    if (std::abs(det) > 1e-8f) {
        const Vec3 tangent = (ab * (c.v - a.v) - ac * (b.v - a.v)) * (1.0f / det);
        const Vec3 bitangent = (ac * (b.u - a.u) - ab * (c.u - a.u)) * (1.0f / det);
        for (const auto index : {ia, ib, ic}) {
            auto& vertex = mesh.vertices[index];
            vertex.tx += tangent.x; vertex.ty += tangent.y; vertex.tz += tangent.z;
            bitangents[index] = bitangents[index] + bitangent;
        }
    }
}
void finalizeMesh(MeshData& mesh, const std::vector<Vec3>& bitangents) {
    mesh.minimum[0] = mesh.maximum[0] = mesh.vertices[0].x;
    mesh.minimum[1] = mesh.maximum[1] = mesh.vertices[0].y;
    mesh.minimum[2] = mesh.maximum[2] = mesh.vertices[0].z;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        const Vec3 n{v.nx, v.ny, v.nz};
        Vec3 t{v.tx, v.ty, v.tz};
        t = t - n * dot(t, n);
        if (!(dot(t, t) > 1e-12f) || !std::isfinite(dot(t, t))) {
            t = cross(std::abs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, n);
        }
        t = normalize(t);
        v.tx = t.x; v.ty = t.y; v.tz = t.z;
        // glTF tangents carry a sign; OBJ and generated tangents start at zero.
        if (v.tw == 0) {
            v.tw = i < bitangents.size() && dot(cross(n, t), bitangents[i]) < 0 ? -1.0f : 1.0f;
        }
        const float coordinates[3] = {v.x, v.y, v.z};
        for (int axis = 0; axis < 3; ++axis) {
            mesh.minimum[axis] = std::min(mesh.minimum[axis], coordinates[axis]);
            mesh.maximum[axis] = std::max(mesh.maximum[axis], coordinates[axis]);
        }
    }
}
bool index(std::string_view value, std::size_t count, std::uint32_t& result) {
    std::int64_t raw = 0;
    if (value.starts_with('+')) { value.remove_prefix(1); }
    if (value.empty()) { return false; }
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), raw);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || raw == 0) { return false; }
    if (raw < 0) {
        if (raw < -static_cast<std::int64_t>(count)) { return false; }
        raw += static_cast<std::int64_t>(count);
    } else {
        if (static_cast<std::uint64_t>(raw) > count) { return false; }
        --raw;
    }
    result = static_cast<std::uint32_t>(raw);
    return true;
}
} // namespace

bool isValidAssetPath(std::string_view path) {
    if (!path.starts_with("Assets/") || path.size() > 1024 ||
        path.find_first_of("\\:\0", 0, 3) != std::string_view::npos) { return false; }
    for (unsigned char ch : path) { if (ch < 32 || ch == 127) { return false; } }
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..") { return false; }
        if (slash == std::string_view::npos) { return true; }
        path.remove_prefix(slash + 1);
    }
    return false;
}
bool isMeshAssetPath(std::string_view path) {
    return isValidAssetPath(path) &&
        (extensionIs(path, ".obj") || extensionIs(path, ".gltf") || extensionIs(path, ".glb"));
}
bool isGltfAssetPath(std::string_view path) {
    return isValidAssetPath(path) && (extensionIs(path, ".gltf") || extensionIs(path, ".glb"));
}
bool isTextureAssetPath(std::string_view path) {
    return isValidAssetPath(path) && (extensionIs(path, ".png") || extensionIs(path, ".jpg") || extensionIs(path, ".jpeg"));
}

bool isShaderAssetPath(std::string_view path) {
    return isValidAssetPath(path) && extensionIs(path, ".slang");
}

bool isMaterialAssetPath(std::string_view path) {
    return isValidAssetPath(path) && extensionIs(path, ".satmat");
}

bool isAudioAssetPath(std::string_view path) {
    return isValidAssetPath(path) && (extensionIs(path, ".wav") || extensionIs(path, ".ogg") ||
        extensionIs(path, ".mp3") || extensionIs(path, ".flac"));
}

namespace {
std::string folded(std::string name) {
    for (auto& ch : name) {
        if (ch >= 'A' && ch <= 'Z') { ch = static_cast<char>(ch + ('a' - 'A')); }
    }
    return name;
}
std::string safeAssetStem(std::string stem) {
    for (auto& ch : stem) {
        if (static_cast<unsigned char>(ch) < 32 || ch == 127 ||
            std::string_view(R"(/\:*?"<>|)").find(ch) != std::string_view::npos) { ch = '_'; }
    }
    const auto last = stem.find_last_not_of(" .");
    if (last == std::string::npos) { return {}; }
    stem.resize(last + 1);
    auto device = folded(stem.substr(0, stem.find('.')));
    if (const auto end = device.find_last_not_of(' '); end != std::string::npos) { device.resize(end + 1); }
    const std::string_view port = std::string_view(device).substr(std::min(device.size(), std::size_t{3}));
    if (device == "con" || device == "prn" || device == "aux" || device == "nul" ||
        ((device.starts_with("com") || device.starts_with("lpt")) &&
         ((port.size() == 1 && port.front() >= '1' && port.front() <= '9') ||
          port == "¹" || port == "²" || port == "³"))) {
        stem.insert(stem.begin(), '_');
    }
    return stem;
}
} // namespace

std::vector<std::string> meshMaterialPaths(std::string_view path, std::span<const std::string> names) {
    const auto folder = path.substr(0, path.find_last_of('/') + 1);
    std::vector<std::string> stems;
    stems.reserve(names.size());
    std::unordered_set<std::string> reserved;
    reserved.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        auto stem = safeAssetStem(names[i]);
        if (stem.empty()) { stem = "Material " + std::to_string(i + 1); }
        reserved.insert(folded(stem));
        stems.push_back(std::move(stem));
    }

    std::unordered_map<std::string, std::size_t> occurrences;
    occurrences.reserve(names.size());
    for (auto& stem : stems) {
        auto& occurrence = occurrences[folded(stem)];
        if (++occurrence > 1) {
            std::string unique;
            do {
                unique = stem + "_" + std::to_string(occurrence);
                if (reserved.insert(folded(unique)).second) { break; }
                ++occurrence;
            } while (true);
            stem = std::move(unique);
        }
        stem.insert(0, folder);
        stem += ".satmat";
    }
    return stems;
}

namespace {
std::string_view objLine(std::string_view& bytes) {
    const auto end = bytes.find('\n');
    auto line = bytes.substr(0, end);
    bytes = end == std::string_view::npos ? std::string_view{} : bytes.substr(end + 1);
    return line.substr(0, line.find('#'));
}
std::string_view trimObjValue(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) { return {}; }
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}
void readMtlProperty(MaterialData& material, std::string_view kind, std::string_view line, std::string_view folder) {
    if (kind == "Kd" || kind == "Ke") {
        float color[3];
        for (auto& channel : color) {
            if (!number(token(line), channel)) { return; }
            channel = std::clamp(channel, 0.0f, 1.0f);
        }
        std::copy_n(color, 3, kind == "Kd" ? material.baseColor : material.emissive);
    } else if (kind == "d" || kind == "Tr" || kind == "Pm" || kind == "Pr" || kind == "Ns") {
        auto value = token(line);
        if (kind == "d" && value == "-halo") { value = token(line); }
        float factor;
        if (!number(value, factor)) { return; }
        if (kind == "Ns") {
            material.roughness = std::sqrt(2.0f / (std::max(factor, 0.0f) + 2.0f));
        } else {
            factor = std::clamp(factor, 0.0f, 1.0f);
            if (kind == "Pm") { material.metallic = factor; }
            else if (kind == "Pr") { material.roughness = factor; }
            else {
                material.baseColor[3] = kind == "d" ? factor : 1.0f - factor;
                material.alphaMode = material.baseColor[3] < 1.0f ? AlphaMode::Blend : AlphaMode::Opaque;
            }
        }
    } else if (kind == "map_Kd" || kind == "map_Ke" || kind == "norm") {
        const auto file = trimObjValue(line);
        if (file.empty() || file.starts_with('-')) { return; }
        auto texture = std::string(folder) + std::string(file);
        if (!isTextureAssetPath(texture)) { return; }
        if (kind == "map_Kd") { material.baseColorTexture = std::move(texture); }
        else if (kind == "map_Ke") { material.emissiveTexture = std::move(texture); }
        else { material.normalTexture = std::move(texture); }
    }
}
} // namespace

bool decodeObjMaterials(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::vector<ObjMaterial>& materials, std::string& error) {
    error.clear();
    std::vector<ObjMaterial> candidates;
    std::unordered_map<std::string, std::size_t> byName;
    std::unordered_set<std::string> libraries;
    const auto folder = path.substr(0, path.find_last_of('/') + 1);
    while (!bytes.empty()) {
        auto line = objLine(bytes);
        if (token(line) != "mtllib") { continue; }
        for (auto file = token(line); !file.empty(); file = token(line)) {
            const auto library = std::string(folder) + std::string(file);
            if (file.starts_with('/') || !isValidAssetPath(library)) {
                error = "Invalid OBJ material library path: " + library;
                return false;
            }
            if (!libraries.insert(library).second) { continue; }
            std::string source;
            if (!reader || reader(library, source, error) != AssetReadResult::Found) {
                if (error.empty()) { error = "Cannot read OBJ material library: " + library; }
                return false;
            }
            const std::string_view textureFolder(library.data(), library.find_last_of('/') + 1);
            std::string_view remaining(source);
            MaterialData* material = nullptr;
            while (!remaining.empty()) {
                auto entry = objLine(remaining);
                const auto kind = token(entry);
                if (kind == "newmtl") {
                    const auto name = trimObjValue(entry);
                    if (name.empty()) {
                        error = "Missing material name in " + library;
                        return false;
                    }
                    const auto [found, inserted] = byName.try_emplace(std::string(name), candidates.size());
                    if (inserted) { candidates.push_back({found->first, {}}); }
                    material = &candidates[found->second].material;
                    *material = {};
                } else if (material) {
                    readMtlProperty(*material, kind, entry, textureFolder);
                }
            }
        }
    }
    materials = std::move(candidates);
    return true;
}

bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error) {
    return decodeObj(bytes, mesh, error, true, true);
}
bool decodeObj(std::string_view bytes, MeshData& mesh, std::string& error, bool rightHanded, bool flipV) {
    error.clear();
    std::size_t lineNumber = 0;
    const auto fail = [&](std::string_view message) {
        error = "OBJ line " + std::to_string(lineNumber) + ": " + std::string(message);
        return false;
    };
    if (bytes.empty() || bytes.size() > kMaxAssetBytes || bytes.find('\0') != std::string_view::npos) {
        return fail("Empty, oversized or binary OBJ");
    }
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<Vec3> normals;
    std::vector<Vec3> bitangents;
    MeshData candidate;
    std::unordered_map<std::uint64_t, std::uint32_t> vertices;
    while (!bytes.empty()) {
        ++lineNumber;
        const auto end = bytes.find('\n');
        auto line = bytes.substr(0, end);
        bytes = end == std::string_view::npos ? std::string_view{} : bytes.substr(end + 1);
        line = line.substr(0, line.find('#'));
        const auto kind = token(line);
        if (kind.empty()) { continue; }
        if (kind == "v" || kind == "vn") {
            std::array<float, 3> value{};
            for (float& component : value) {
                if (!number(token(line), component)) { return fail("Expected three finite coordinates"); }
            }
            // Optional homogeneous W must be 1. Vertex colors are not supported.
            if (const auto extra = token(line); !extra.empty()) {
                float w = 0;
                if (kind != "v" || !number(extra, w) || w != 1 || !token(line).empty()) {
                    return fail("Only Cartesian XYZ positions and normals are supported");
                }
            }
            if (kind == "v") {
                if (positions.size() >= kMaxVertices) { return fail("Position limit exceeded"); }
                if (rightHanded) { value[2] = -value[2]; }
                positions.push_back(value);
            } else {
                if (normals.size() >= kMaxVertices) { return fail("Normal limit exceeded"); }
                normals.push_back(unitVector({value[0], value[1], rightHanded ? -value[2] : value[2]}));
            }
        } else if (kind == "vt") {
            std::array<float, 2> uv{};
            if (!number(token(line), uv[0]) || !number(token(line), uv[1])) { return fail("Expected UV coordinates"); }
            if (const auto extra = token(line); !extra.empty()) {
                float w = 0;
                if (!number(extra, w) || w != 0 || !token(line).empty()) { return fail("Only 2D UVs are supported"); }
            }
            if (uvs.size() >= kMaxVertices) { return fail("UV limit exceeded"); }
            if (flipV) { uv[1] = 1.0f - uv[1]; }
            uvs.push_back(uv);
        } else if (kind == "f") {
            std::array<std::uint32_t, 256> face{};
            std::size_t count = 0;
            for (auto reference = token(line); !reference.empty(); reference = token(line)) {
                if (count == face.size()) { return fail("Face exceeds 256 corners"); }
                const auto slash = reference.find('/');
                std::uint32_t position = 0;
                std::uint32_t uv = std::numeric_limits<std::uint32_t>::max();
                std::uint32_t normal = std::numeric_limits<std::uint32_t>::max();
                if (!index(reference.substr(0, slash), positions.size(), position)) { return fail("Invalid position index"); }
                if (slash != std::string_view::npos) {
                    auto rest = reference.substr(slash + 1);
                    const auto second = rest.find('/');
                    const auto uvText = rest.substr(0, second);
                    if (!uvText.empty() && !index(uvText, uvs.size(), uv)) { return fail("Invalid UV index"); }
                    if (second != std::string_view::npos) {
                        if (!index(rest.substr(second + 1), normals.size(), normal)) { return fail("Invalid normal index"); }
                    } else if (uvText.empty()) { return fail("Missing UV index"); }
                }
                // Three bounded indices fit in 20 bits each. Generated normals are face-local.
                constexpr std::uint32_t kMissing = (1u << 20) - 1;
                const bool hasNormal = normal < normals.size();
                const auto key = (static_cast<std::uint64_t>(position) << 40) |
                    (static_cast<std::uint64_t>(std::min(uv, kMissing)) << 20) | std::min(normal, kMissing);
                auto found = hasNormal ? vertices.find(key) : vertices.end();
                if (found == vertices.end()) {
                    if (candidate.vertices.size() >= kMaxVertices) { return fail("Vertex limit exceeded"); }
                    const auto& p = positions[position];
                    const auto tex = uv == std::numeric_limits<std::uint32_t>::max() ? std::array<float, 2>{} : uvs[uv];
                    const auto next = static_cast<std::uint32_t>(candidate.vertices.size());
                    const Vec3 n = hasNormal ? normals[normal] : Vec3{};
                    candidate.vertices.push_back({p[0], p[1], p[2], n.x, n.y, n.z, tex[0], tex[1], 0, 0, 0, 0});
                    bitangents.emplace_back();
                    if (hasNormal) { vertices.emplace(key, next); }
                    face[count++] = next;
                    continue;
                }
                face[count++] = found->second;
            }
            if (count < 3) { return fail("Face requires at least three corners"); }
            if (candidate.indices.size() + (count - 2) * 3 > kMaxIndices) { return fail("Triangle index limit exceeded"); }
            const auto positionOf = [&](std::uint32_t i) {
                const auto& v = candidate.vertices[i];
                return Vec3{v.x, v.y, v.z};
            };
            Vec3 faceNormal{};
            for (std::size_t i = 1; i + 1 < count; ++i) {
                faceNormal = faceNormal + cross(positionOf(face[i]) - positionOf(face[0]),
                    positionOf(face[i + 1]) - positionOf(face[0]));
            }
            faceNormal = unitVector(faceNormal * (rightHanded ? -1.0f : 1.0f));
            for (std::size_t i = 0; i < count; ++i) {
                auto& v = candidate.vertices[face[i]];
                if (v.nx == 0 && v.ny == 0 && v.nz == 0) {
                    v.nx = faceNormal.x; v.ny = faceNormal.y; v.nz = faceNormal.z;
                }
            }
            for (std::size_t i = 1; i + 1 < count; ++i) {
                accumulateTangent(candidate, bitangents, face[0], face[i], face[i + 1]);
                if (rightHanded) { candidate.indices.insert(candidate.indices.end(), {face[0], face[i + 1], face[i]}); }
                else { candidate.indices.insert(candidate.indices.end(), {face[0], face[i], face[i + 1]}); }
            }
        } else if (kind != "o" && kind != "g" && kind != "s" && kind != "mtllib" && kind != "usemtl") {
            return fail("Unsupported record (static polygon OBJ required)");
        }
    }
    if (candidate.indices.empty()) { return fail("No triangles found"); }
    finalizeMesh(candidate, bitangents);
    mesh = std::move(candidate);
    return true;
}

namespace {
using GltfDocument = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>;

const char* gltfResult(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short: return "truncated data or out-of-range accessor";
    case cgltf_result_unknown_format: return "unsupported file format or URI";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF structure";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 2.0 is required";
    default: return "could not read data";
    }
}
GltfDocument parseGltf(std::string_view bytes, const cgltf_options& options, std::string& error) {
    error.clear();
    cgltf_data* parsed = nullptr;
    if (bytes.empty() || bytes.size() > kMaxAssetBytes) {
        error = "glTF source is empty or exceeds 64 MiB";
    } else if (const auto result = cgltf_parse(&options, bytes.data(), bytes.size(), &parsed);
               result != cgltf_result_success) {
        error = std::string("Cannot parse glTF: ") + gltfResult(result);
    }
    return GltfDocument(parsed, cgltf_free);
}

std::string gltfAssetFolder(std::string_view path) {
    return std::string(path.substr(0, path.find_last_of('/') + 1));
}
bool resolveGltfUri(std::string_view folder, std::string uri, std::string& path, std::string& error) {
    uri.resize(cgltf_decode_uri(uri.data()));
    path = std::string(folder) + uri;
    if (!isValidAssetPath(path) || uri.empty() || uri.find_first_of("?#") != std::string::npos) {
        error = "Invalid glTF companion URI: " + uri;
        return false;
    }
    return true;
}

struct GltfReadContext {
    const AssetReader& reader;
    std::string& error;
};
cgltf_result readGltfBuffer(const cgltf_memory_options*, const cgltf_file_options* file,
    const char* path, cgltf_size* size, void** data) {
    auto& context = *static_cast<GltfReadContext*>(file->user_data);
    try {
        // cgltf has already joined the model folder and decoded the URI.
        if (!isValidAssetPath(path)) {
            context.error = std::string("Invalid glTF buffer path: ") + path;
            return cgltf_result_io_error;
        }
        std::string bytes, error;
        const auto result = context.reader ? context.reader(path, bytes, error) : AssetReadResult::Missing;
        if (result != AssetReadResult::Found) {
            context.error = std::string("Cannot read glTF buffer ") + path + ": " +
                (error.empty() ? (result == AssetReadResult::Missing ? "file not found" : "reader failed") : error);
            return cgltf_result_io_error;
        }
        if (bytes.size() < *size || bytes.size() > kMaxAssetBytes) {
            context.error = std::string("glTF buffer is truncated or exceeds 64 MiB: ") + path;
            return cgltf_result_io_error;
        }
        void* buffer = std::malloc(*size);
        if (!buffer) { return cgltf_result_out_of_memory; }
        std::memcpy(buffer, bytes.data(), *size);
        *data = buffer;
        return cgltf_result_success;
    } catch (...) {
        return cgltf_result_io_error;
    }
}
void releaseGltfBuffer(const cgltf_memory_options*, const cgltf_file_options*, void* data) {
    std::free(data);
}

bool unsignedIndexType(cgltf_component_type type) {
    return type == cgltf_component_type_r_8u || type == cgltf_component_type_r_16u ||
        type == cgltf_component_type_r_32u;
}
bool validAccessorRange(const cgltf_buffer_view& view, std::size_t offset, std::size_t count,
    std::size_t stride, std::size_t elementSize, std::size_t alignment) {
    // Subtraction and division keep attacker-controlled offsets and counts from
    // overflowing before cgltf_validate reads the packed index data.
    return count != 0 && offset <= view.size && elementSize <= view.size - offset &&
        stride >= elementSize && (view.offset + offset) % alignment == 0 &&
        stride % alignment == 0 && count - 1 <= (view.size - offset - elementSize) / stride;
}
bool validGltfLayout(const cgltf_data& data) {
    for (std::size_t i = 0; i < data.buffer_views_count; ++i) {
        const auto& view = data.buffer_views[i];
        if (view.offset > view.buffer->size || view.size > view.buffer->size - view.offset) { return false; }
    }
    for (std::size_t i = 0; i < data.accessors_count; ++i) {
        const auto& accessor = data.accessors[i];
        const auto alignment = cgltf_component_size(accessor.component_type);
        const auto elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
        if (!accessor.count || !alignment || !elementSize) { return false; }
        if (accessor.buffer_view && !validAccessorRange(*accessor.buffer_view, accessor.offset,
            accessor.count, accessor.stride, elementSize, alignment)) { return false; }
        if (accessor.is_sparse) {
            const auto& sparse = accessor.sparse;
            const auto indexSize = cgltf_component_size(sparse.indices_component_type);
            if (sparse.count > accessor.count || !unsignedIndexType(sparse.indices_component_type) ||
                !validAccessorRange(*sparse.indices_buffer_view, sparse.indices_byte_offset,
                    sparse.count, indexSize, indexSize, indexSize) ||
                !validAccessorRange(*sparse.values_buffer_view, sparse.values_byte_offset,
                    sparse.count, elementSize, elementSize, alignment)) { return false; }
        }
    }
    return true;
}

GltfDocument loadGltf(std::string_view bytes, std::string_view path, GltfReadContext& context) {
    auto& error = context.error;
    cgltf_options options{};
    options.file = {readGltfBuffer, releaseGltfBuffer, &context};
    auto data = parseGltf(bytes, options, error);
    if (!data) { return data; }
    const auto fail = [&](std::string message) {
        error = std::move(message);
        return GltfDocument(nullptr, cgltf_free);
    };
    if (!isGltfAssetPath(path)) { return fail("Invalid glTF asset path"); }
    for (std::size_t i = 0; i < data->meshes_count; ++i) {
        const auto& mesh = data->meshes[i];
        for (std::size_t j = 0; j < mesh.primitives_count; ++j) {
            if (mesh.primitives[j].has_draco_mesh_compression) {
                return fail("glTF uses Draco compression; export the model without Draco compression");
            }
        }
    }
    for (std::size_t i = 0; i < data->buffer_views_count; ++i) {
        const auto& view = data->buffer_views[i];
        if (view.has_meshopt_compression) {
            return fail("glTF uses meshopt compression; export the model without meshopt compression");
        }
        for (std::size_t j = 0; j < view.extensions_count; ++j) {
            if (std::string_view(view.extensions[j].name) == "KHR_meshopt_compression") {
                return fail("glTF uses meshopt compression; export the model without meshopt compression");
            }
        }
    }
    const auto folder = gltfAssetFolder(path);
    std::size_t bufferBytes = 0;
    for (std::size_t i = 0; i < data->buffers_count; ++i) {
        auto& buffer = data->buffers[i];
        if (!buffer.size || buffer.size > kMaxAssetBytes - bufferBytes) {
            return fail("glTF buffer data is empty or exceeds 64 MiB in total");
        }
        bufferBytes += buffer.size;
        if (buffer.uri) {
            const std::string_view uri(buffer.uri, cgltf_decode_string(buffer.uri));
            if (uri.find('\0') != std::string_view::npos) { return fail("Invalid glTF buffer URI"); }
            if (!uri.starts_with("data:")) {
                // Check decoded URI length before cgltf passes a C string to the
                // callback: an escaped NUL must not hide the rest of the URI.
                std::string resolved;
                if (!resolveGltfUri(folder, std::string(uri), resolved, error)) {
                    return GltfDocument(nullptr, cgltf_free);
                }
            }
        } else if (i != 0 || !data->bin) {
            return fail("glTF buffer has no URI or GLB binary chunk");
        }
    }
    if (!validGltfLayout(*data)) { return fail("Invalid glTF buffer view or accessor range"); }
    const std::string source(path);
    if (const auto result = cgltf_load_buffers(&options, data.get(), source.c_str()); result != cgltf_result_success) {
        if (error.empty()) { error = std::string("Cannot load glTF buffers: ") + gltfResult(result); }
        return GltfDocument(nullptr, cgltf_free);
    }
    if (const auto result = cgltf_validate(data.get()); result != cgltf_result_success) {
        return fail(std::string("Cannot validate glTF: ") + gltfResult(result));
    }
    return data;
}

bool unpackIndices(const cgltf_accessor& accessor, std::uint32_t* output) {
    return cgltf_accessor_unpack_indices(&accessor, output, sizeof(*output), accessor.count) == accessor.count;
}

// cgltf 1.15 uses the base accessor's stride for sparse float values and its
// index unpacker only accepts dense accessors. Read the packed overrides as
// dense views and apply them after reading the base.
template<class T, class Unpack>
bool unpackGltfAccessor(const cgltf_accessor& accessor, std::size_t components, T* output, Unpack unpack) {
    auto base = accessor;
    base.is_sparse = false;
    if (base.buffer_view) {
        if (!unpack(base, output)) { return false; }
    } else {
        std::fill_n(output, accessor.count * components, T{});
    }
    if (!accessor.is_sparse) { return true; }
    const auto& sparse = accessor.sparse;
    cgltf_accessor indexAccessor{};
    indexAccessor.type = cgltf_type_scalar;
    indexAccessor.component_type = sparse.indices_component_type;
    indexAccessor.count = sparse.count;
    indexAccessor.buffer_view = sparse.indices_buffer_view;
    indexAccessor.offset = sparse.indices_byte_offset;
    indexAccessor.stride = cgltf_component_size(indexAccessor.component_type);
    std::vector<std::uint32_t> indices(sparse.count);
    if (!unpackIndices(indexAccessor, indices.data())) { return false; }
    auto valuesAccessor = base;
    valuesAccessor.count = sparse.count;
    valuesAccessor.buffer_view = sparse.values_buffer_view;
    valuesAccessor.offset = sparse.values_byte_offset;
    valuesAccessor.stride = cgltf_calc_size(base.type, base.component_type);
    std::vector<T> values(sparse.count * components);
    if (!unpack(valuesAccessor, values.data())) { return false; }
    for (std::size_t i = 0; i < sparse.count; ++i) {
        if (indices[i] >= accessor.count || (i && indices[i] <= indices[i - 1])) { return false; }
        std::copy_n(values.data() + i * components, components, output + indices[i] * components);
    }
    return true;
}
bool readAttribute(const cgltf_accessor* accessor, cgltf_type type, std::size_t count, std::vector<float>& values) {
    values.clear();
    if (!accessor) { return true; }
    if (accessor->type != type || accessor->count != count) { return false; }
    const auto components = cgltf_num_components(type);
    values.resize(count * components);
    return unpackGltfAccessor(*accessor, components, values.data(),
        [components](const cgltf_accessor& source, float* output) {
            const auto size = source.count * components;
            return cgltf_accessor_unpack_floats(&source, output, size) == size;
        }) && std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}


Mat4 leftHandedMatrix(const float* values) {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result.m[column * 4 + row] = values[column * 4 + row] * ((column == 2) != (row == 2) ? -1.0f : 1.0f);
        }
    }
    return result;
}
bool buildAnimationJoints(const cgltf_data& data, AnimationData& animation, std::vector<std::vector<std::uint16_t>>& skinSlots, std::string& error) {
    if (!data.animations_count && !data.skins_count) { return true; }
    if (data.nodes_count > kMaxAnimationJoints) { error = "Animated glTF exceeds 256 nodes"; return false; }
    animation.joints.resize(data.nodes_count);
    std::unordered_set<std::string> names;
    for (std::size_t index = 0; index < data.nodes_count; ++index) {
        const auto& node = data.nodes[index];
        auto& joint = animation.joints[index];
        joint.name = node.name && *node.name ? node.name : "Node " + std::to_string(index + 1);
        if (!names.insert(joint.name).second) { error = "Duplicate animated glTF node name: " + joint.name; return false; }
        joint.parent = node.parent ? static_cast<int>(node.parent - data.nodes) : -1;
        joint.restTRS.translation = {node.translation[0], node.translation[1], -node.translation[2]};
        joint.restTRS.scale = {node.scale[0], node.scale[1], node.scale[2]};
        std::copy_n(node.rotation, 4, joint.restTRS.rotation);
        joint.restTRS.rotation[0] *= -1;
        joint.restTRS.rotation[1] *= -1;
        float local[16], world[16];
        cgltf_node_transform_local(&node, local);
        cgltf_node_transform_world(&node, world);
        joint.restLocal = leftHandedMatrix(local);
        joint.inverseBind = inverse(leftHandedMatrix(world));
        for (float value : joint.restLocal.m) {
            if (!std::isfinite(value)) { error = "Nonfinite transform on glTF node " + joint.name; return false; }
        }
    }
    std::vector<bool> bound(data.nodes_count);
    skinSlots.resize(data.skins_count);
    for (std::size_t skinIndex = 0; skinIndex < data.skins_count; ++skinIndex) {
        const auto& skin = data.skins[skinIndex];
        std::vector<float> matrices;
        if (skin.joints_count > kMaxAnimationJoints ||
            !readAttribute(skin.inverse_bind_matrices, cgltf_type_mat4, skin.joints_count, matrices)) {
            error = "Invalid inverse bind accessor in glTF skin " + std::to_string(skinIndex); return false;
        }
        for (std::size_t index = 0; index < skin.joints_count; ++index) {
            const auto nodeIndex = static_cast<std::size_t>(skin.joints[index] - data.nodes);
            const Mat4 matrix = matrices.empty() ? Mat4{} : leftHandedMatrix(matrices.data() + index * 16);
            std::size_t slot = nodeIndex;
            if (data.nodes[nodeIndex].mesh || (bound[nodeIndex] &&
                !std::equal(std::begin(matrix.m), std::end(matrix.m), std::begin(animation.joints[nodeIndex].inverseBind.m)))) {
                // A palette slot may need a different inverse bind while sharing a node pose.
                if (animation.joints.size() == kMaxAnimationJoints) { error = "glTF skin bindings exceed 256 palette joints"; return false; }
                AnimationJoint alias;
                alias.name = animation.joints[nodeIndex].name + "#skin" + std::to_string(skinIndex);
                while (!names.insert(alias.name).second) { alias.name += "_"; }
                alias.parent = static_cast<int>(nodeIndex);
                alias.inverseBind = matrix;
                slot = animation.joints.size();
                animation.joints.push_back(std::move(alias));
            } else {
                bound[nodeIndex] = true;
                animation.joints[nodeIndex].inverseBind = matrix;
            }
            skinSlots[skinIndex].push_back(static_cast<std::uint16_t>(slot));
        }
    }
    return true;
}
bool readAnimationClips(const cgltf_data& data, AnimationData& animation, std::string& error) {
    for (std::size_t index = 0; index < data.animations_count; ++index) {
        const auto& source = data.animations[index];
        AnimationClipAsset asset;
        asset.clip.name = source.name && *source.name ? source.name : "Clip " + std::to_string(index + 1);
        for (const auto& joint : animation.joints) { asset.jointNames.push_back(joint.name); }
        if (source.channels_count > kMaxAnimationJoints * 3) { error = "Too many glTF animation channels"; return false; }
        for (std::size_t channelIndex = 0; channelIndex < source.channels_count; ++channelIndex) {
            const auto& input = source.channels[channelIndex];
            if (input.target_path == cgltf_animation_path_type_weights) { continue; }
            if (!input.target_node || !input.sampler || !input.sampler->input || !input.sampler->output || input.target_node->has_matrix) {
                error = "Invalid animation target/sampler in glTF clip " + asset.clip.name; return false;
            }
            AnimationChannel channel;
            channel.joint = static_cast<std::uint16_t>(input.target_node - data.nodes);
            switch (input.target_path) {
            case cgltf_animation_path_type_translation: channel.path = AnimationPath::Translation; break;
            case cgltf_animation_path_type_rotation: channel.path = AnimationPath::Rotation; break;
            case cgltf_animation_path_type_scale: channel.path = AnimationPath::Scale; break;
            default: error = "Unsupported glTF animation path"; return false;
            }
            switch (input.sampler->interpolation) {
            case cgltf_interpolation_type_step: channel.interpolation = AnimationInterpolation::Step; break;
            case cgltf_interpolation_type_linear: channel.interpolation = AnimationInterpolation::Linear; break;
            case cgltf_interpolation_type_cubic_spline: channel.interpolation = AnimationInterpolation::CubicSpline; break;
            default: error = "Invalid glTF interpolation"; return false;
            }
            const auto count = input.sampler->input->count;
            const auto multiplier = channel.interpolation == AnimationInterpolation::CubicSpline ? 3u : 1u;
            const bool rotation = channel.path == AnimationPath::Rotation;
            if (count > kMaxAnimationKeys || !count || input.sampler->input->component_type != cgltf_component_type_r_32f ||
                input.sampler->output->component_type != cgltf_component_type_r_32f ||
                !readAttribute(input.sampler->input, cgltf_type_scalar, count, channel.times) ||
                !readAttribute(input.sampler->output, rotation ? cgltf_type_vec4 : cgltf_type_vec3, count * multiplier, channel.values)) {
                error = "Invalid animation accessor for glTF node " + animation.joints[channel.joint].name; return false;
            }
            for (std::size_t offset = 0; offset < channel.values.size(); offset += rotation ? 4 : 3) {
                if (rotation) { channel.values[offset] *= -1; channel.values[offset + 1] *= -1; }
                else if (channel.path == AnimationPath::Translation) { channel.values[offset + 2] *= -1; }
            }
            asset.clip.duration = std::max(asset.clip.duration, channel.times.back());
            asset.clip.channels.push_back(std::move(channel));
        }
        asset.endSeconds = asset.clip.duration;
        if (!validateAnimationClip(asset, error)) { error = "glTF clip " + asset.clip.name + ": " + error; return false; }
        animation.clips.push_back(std::move(asset.clip));
    }
    return true;
}
bool appendSkin(const cgltf_node& node, const cgltf_primitive& primitive, const cgltf_data& data,
    std::size_t firstVertex, std::size_t sourceVertexCount, std::span<const std::uint32_t> vertexSources,
    MeshData& mesh, std::span<const std::uint16_t> skinSlots, std::string& error) {
    const auto count = mesh.vertices.size() - firstVertex;
    std::vector<float> joints, weights;
    if (node.skin) {
        const auto* jointAccessor = cgltf_find_accessor(&primitive, cgltf_attribute_type_joints, 0);
        const auto* weightAccessor = cgltf_find_accessor(&primitive, cgltf_attribute_type_weights, 0);
        if (!jointAccessor || !weightAccessor || jointAccessor->normalized ||
            (jointAccessor->component_type != cgltf_component_type_r_8u && jointAccessor->component_type != cgltf_component_type_r_16u) ||
            !readAttribute(jointAccessor, cgltf_type_vec4, sourceVertexCount, joints) ||
            !readAttribute(weightAccessor, cgltf_type_vec4, sourceVertexCount, weights)) {
            error = "Invalid JOINTS_0/WEIGHTS_0 on glTF node " + std::to_string(&node - data.nodes); return false;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        MeshSkinVertex skin;
        if (!node.skin) {
            skin.joints[0] = static_cast<std::uint8_t>(&node - data.nodes);
            skin.weights[0] = 255;
        } else {
            const auto source = vertexSources.empty() ? index : vertexSources[index];
            float total = 0;
            for (int axis = 0; axis < 4; ++axis) {
                const float joint = joints[source * 4 + axis];
                const float weight = weights[source * 4 + axis];
                if (joint < 0 || joint >= node.skin->joints_count || std::floor(joint) != joint || weight < 0) {
                    error = "Out-of-range skin joint or weight on glTF node " + std::to_string(&node - data.nodes); return false;
                }
                skin.joints[axis] = static_cast<std::uint8_t>(skinSlots[static_cast<std::size_t>(joint)]);
                total += weight;
            }
            if (!(total > 0) || !std::isfinite(total)) { error = "Invalid glTF skin weight sum"; return false; }
            float fractions[4]{};
            unsigned sum = 0;
            for (int axis = 0; axis < 4; ++axis) {
                const float value = weights[source * 4 + axis] / total * 255;
                skin.weights[axis] = static_cast<std::uint8_t>(std::floor(value));
                fractions[axis] = value - skin.weights[axis];
                sum += skin.weights[axis];
            }
            while (sum < 255) {
                const auto axis = std::max_element(std::begin(fractions), std::end(fractions)) - std::begin(fractions);
                ++skin.weights[axis]; fractions[axis] = -1; ++sum;
            }
        }
        mesh.skinVertices.push_back(skin);
        const auto& vertex = mesh.vertices[firstVertex + index];
        const Vec3 point{vertex.x, vertex.y, vertex.z};
        for (int axis = 0; axis < 4; ++axis) {
            if (!skin.weights[axis]) { continue; }
            auto& bounds = mesh.jointBounds[skin.joints[axis]];
            if (!bounds.valid) { bounds.minimum = bounds.maximum = point; bounds.valid = true; }
            bounds.minimum = {std::min(bounds.minimum.x, point.x), std::min(bounds.minimum.y, point.y), std::min(bounds.minimum.z, point.z)};
            bounds.maximum = {std::max(bounds.maximum.x, point.x), std::max(bounds.maximum.y, point.y), std::max(bounds.maximum.z, point.z)};
        }
    }
    return true;
}

bool decodeGltfMaterialSources(const cgltf_data& data, std::string_view path, const AssetReader& reader,
    std::span<const cgltf_material* const> sources, std::span<MaterialData> materials,
    std::vector<GltfImage>& images, std::string& error, bool extracting);

struct GltfMeshBuilder {
    MeshData mesh;
    std::vector<Vec3> bitangents;
    std::vector<float> positions, normals, uvs, secondaryUvs, tangents, colors;
    std::vector<std::uint32_t> indices;
};

bool appendGltfPrimitive(const cgltf_primitive& primitive, const cgltf_data& data, const cgltf_node& node,
    const float (&world)[16], GltfMeshBuilder& builder, std::span<const std::uint16_t> skinSlots, std::string& error) {
    const auto fail = [&](const char* message) { error = message; return false; };
    const auto* position = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    if (!position) { return fail("glTF triangle primitive is missing POSITION"); }
    const auto count = position->count;
    const auto indexCount = primitive.indices ? primitive.indices->count : count;
    auto& mesh = builder.mesh;
    if (indexCount % 3 != 0) { return fail("glTF triangle index count must be a multiple of three"); }
    if (indexCount > kMaxIndices - mesh.indices.size() || count > kMaxVertices) {
        return fail("glTF mesh exceeds the vertex or index limit");
    }

    const Vec3 x{world[0], world[1], world[2]}, y{world[4], world[5], world[6]}, z{world[8], world[9], world[10]};
    const Vec3 translation{world[12], world[13], world[14]};
    const Vec3 nx = cross(y, z), ny = cross(z, x), nz = cross(x, y);
    const float determinant = dot(x, nx);
    if (!std::isfinite(determinant)) { return fail("Invalid glTF node transform"); }
    const float orientation = determinant < 0 ? -1.0f : 1.0f;
    const auto* normal = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    const auto* uv = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
    const auto* secondaryUv = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 1);
    const auto* tangent = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
    const auto* color = cgltf_find_accessor(&primitive, cgltf_attribute_type_color, 0);
    const auto colorType = color && color->type == cgltf_type_vec3 ? cgltf_type_vec3 : cgltf_type_vec4;
    const auto colorComponents = cgltf_num_components(colorType);
    const bool flat = !normal || determinant == 0;
    if ((flat ? indexCount : count) > kMaxVertices - mesh.vertices.size()) {
        return fail("glTF mesh exceeds the vertex limit");
    }
    if (!readAttribute(position, cgltf_type_vec3, count, builder.positions) ||
        !readAttribute(normal, cgltf_type_vec3, count, builder.normals) ||
        !readAttribute(uv, cgltf_type_vec2, count, builder.uvs) ||
        !readAttribute(secondaryUv, cgltf_type_vec2, count, builder.secondaryUvs) ||
        !readAttribute(tangent, cgltf_type_vec4, count, builder.tangents) ||
        !readAttribute(color, colorType, count, builder.colors)) {
        return fail("Invalid glTF vertex attributes");
    }
    auto& indices = builder.indices;
    indices.resize(indexCount);
    if (primitive.indices) {
        if (!unpackGltfAccessor(*primitive.indices, 1, indices.data(), unpackIndices) ||
            std::any_of(indices.begin(), indices.end(), [count](std::uint32_t index) { return index >= count; })) {
            return fail("Invalid glTF triangle index");
        }
    } else {
        for (std::size_t i = 0; i < indexCount; ++i) { indices[i] = static_cast<std::uint32_t>(i); }
    }

    const auto makeVertex = [&](std::size_t index) {
        const auto* p = builder.positions.data() + index * 3;
        const Vec3 transformed = x * p[0] + y * p[1] + z * p[2] + translation;
        MeshVertex vertex{transformed.x, transformed.y, -transformed.z, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (!flat) {
            const auto* n = builder.normals.data() + index * 3;
            // The cofactor matrix is inverse-transpose times the determinant.
            // Its positive magnitude cancels during normalization.
            const auto cofactorNormal = (nx * n[0] + ny * n[1] + nz * n[2]) * orientation;
            const auto transformedNormal = unitVector({cofactorNormal.x, cofactorNormal.y, -cofactorNormal.z});
            vertex.nx = transformedNormal.x; vertex.ny = transformedNormal.y; vertex.nz = transformedNormal.z;
        }
        if (uv) {
            vertex.u = builder.uvs[index * 2];
            vertex.v = builder.uvs[index * 2 + 1];
        }
        if (secondaryUv) {
            vertex.u1 = builder.secondaryUvs[index * 2];
            vertex.v1 = builder.secondaryUvs[index * 2 + 1];
        }
        if (color) {
            for (std::size_t i = 0; i < colorComponents; ++i) {
                vertex.color[i] = std::clamp(builder.colors[index * colorComponents + i], 0.0f, 1.0f);
            }
        }
        if (tangent) {
            const auto* t = builder.tangents.data() + index * 4;
            const Vec3 transformedTangent = x * t[0] + y * t[1] + z * t[2];
            vertex.tx = transformedTangent.x; vertex.ty = transformedTangent.y; vertex.tz = -transformedTangent.z;
            vertex.tw = t[3] < 0 ? orientation : -orientation;
        }
        return vertex;
    };
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    if (!flat) {
        for (std::size_t i = 0; i < count; ++i) { mesh.vertices.push_back(makeVertex(i)); }
    }
    if (!tangent && uv) { builder.bitangents.resize(first + (flat ? indexCount : count)); }
    for (std::size_t i = 0; i < indexCount; i += 3) {
        if (orientation > 0) { std::swap(indices[i + 1], indices[i + 2]); }
        auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (flat) {
            auto va = makeVertex(a), vb = makeVertex(b), vc = makeVertex(c);
            const auto n = unitVector(cross(positionOf(vb) - positionOf(va), positionOf(vc) - positionOf(va)));
            va.nx = vb.nx = vc.nx = n.x;
            va.ny = vb.ny = vc.ny = n.y;
            va.nz = vb.nz = vc.nz = n.z;
            a = static_cast<std::uint32_t>(mesh.vertices.size());
            b = a + 1;
            c = a + 2;
            mesh.vertices.insert(mesh.vertices.end(), {va, vb, vc});
        } else {
            a += first; b += first; c += first;
        }
        if (!tangent && uv) { accumulateTangent(mesh, builder.bitangents, a, b, c); }
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
    }
    // Flat shading expands vertices in triangle order, including the winding conversion.
    return mesh.restPalette.empty() || appendSkin(node, primitive, data, first, count,
        flat ? std::span<const std::uint32_t>(indices) : std::span<const std::uint32_t>{}, mesh, skinSlots, error);
}
} // namespace

bool decodeGltf(std::string_view bytes, std::string_view path, const AssetReader& reader,
    MeshData& mesh, std::string& error) {
    GltfReadContext context{reader, error};
    const auto data = loadGltf(bytes, path, context);
    if (!data) { return false; }
    AnimationData animation;
    std::vector<std::vector<std::uint16_t>> skinSlots;
    if (!buildAnimationJoints(*data, animation, skinSlots, error) || !readAnimationClips(*data, animation, error)) { return false; }
    std::vector<std::pair<const cgltf_node*, std::uint32_t>> nodes;
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count ? data->scenes : nullptr);
    if (scene) {
        for (std::size_t i = scene->nodes_count; i > 0; --i) { nodes.emplace_back(scene->nodes[i - 1], 0); }
    } else {
        for (std::size_t i = data->nodes_count; i > 0; --i) {
            if (!data->nodes[i - 1].parent) { nodes.emplace_back(&data->nodes[i - 1], 0); }
        }
    }
    GltfMeshBuilder builder;
    builder.mesh.jointBounds.resize(animation.joints.size());
    for (std::size_t index = 0; index < animation.joints.size(); ++index) {
        float world[16];
        const auto nodeIndex = index < data->nodes_count ? index : static_cast<std::size_t>(animation.joints[index].parent);
        cgltf_node_transform_world(&data->nodes[nodeIndex], world);
        builder.mesh.restPalette.push_back(leftHandedMatrix(world) * animation.joints[index].inverseBind);
    }
    std::unordered_map<const cgltf_material*, std::uint32_t> materialIndices;
    std::vector<const cgltf_material*> materialSources;
    std::vector<bool> visited(data->nodes_count);
    while (!nodes.empty()) {
        const auto [node, parent] = nodes.back();
        nodes.pop_back();
        const auto sourceIndex = static_cast<std::size_t>(node - data->nodes);
        if (visited[sourceIndex]) {
            error = "glTF scene references the same node more than once";
            return false;
        }
        visited[sourceIndex] = true;
        const auto id = static_cast<std::uint32_t>(sourceIndex + 1);
        for (std::size_t i = node->children_count; i > 0; --i) {
            nodes.emplace_back(node->children[i - 1], id);
        }
        float world[16];
        cgltf_node_transform_world(node, world);
        const auto firstVertex = builder.mesh.vertices.size();
        MeshNode part;
        part.id = id;
        part.parent = parent;
        part.name = node->name && *node->name ? node->name : "Node " + std::to_string(id);
        part.firstSection = static_cast<std::uint32_t>(builder.mesh.sections.size());
        part.origin[0] = world[12];
        part.origin[1] = world[13];
        part.origin[2] = -world[14];
        const Vec3 nodeScale{
            length({world[0], world[1], world[2]}),
            length({world[4], world[5], world[6]}),
            length({world[8], world[9], world[10]})
        };
        for (std::size_t i = 0; node->mesh && i < node->mesh->primitives_count; ++i) {
            const auto& primitive = node->mesh->primitives[i];
            if (primitive.type != cgltf_primitive_type_triangles) { continue; }
            const auto first = static_cast<std::uint32_t>(builder.mesh.indices.size());
            const Mat4 identity;
            const auto slots = node->skin ? std::span<const std::uint16_t>(skinSlots[node->skin - data->skins]) : std::span<const std::uint16_t>{};
            if (!appendGltfPrimitive(primitive, *data, *node, node->skin ? identity.m : world, builder, slots, error)) { return false; }
            auto materialIndex = kNoMeshMaterial;
            if (primitive.material) {
                const auto [found, inserted] = materialIndices.try_emplace(primitive.material,
                    static_cast<std::uint32_t>(materialSources.size()));
                if (inserted) { materialSources.push_back(primitive.material); }
                materialIndex = found->second;
            }
            const auto count = static_cast<std::uint32_t>(builder.mesh.indices.size()) - first;
            if (count != 0) { builder.mesh.sections.push_back({first, count, materialIndex, nodeScale}); }
        }
        part.sectionCount = static_cast<std::uint32_t>(builder.mesh.sections.size()) - part.firstSection;
        if (part.sectionCount != 0) {
            const auto& first = builder.mesh.vertices[firstVertex];
            part.minimum[0] = part.maximum[0] = first.x;
            part.minimum[1] = part.maximum[1] = first.y;
            part.minimum[2] = part.maximum[2] = first.z;
            for (std::size_t i = firstVertex + 1; i < builder.mesh.vertices.size(); ++i) {
                const auto& vertex = builder.mesh.vertices[i];
                const float values[] = {vertex.x, vertex.y, vertex.z};
                for (int axis = 0; axis < 3; ++axis) {
                    part.minimum[axis] = std::min(part.minimum[axis], values[axis]);
                    part.maximum[axis] = std::max(part.maximum[axis], values[axis]);
                }
            }
        }
        builder.mesh.nodes.push_back(std::move(part));
    }
    if (builder.mesh.indices.empty()) { error = "No triangles found in glTF scene"; return false; }
    for (const auto& vertex : builder.mesh.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)) {
            error = "glTF node transform produced nonfinite coordinates";
            return false;
        }
    }
    finalizeMesh(builder.mesh, builder.bitangents);
    builder.mesh.materials.resize(materialSources.size());
    if (!decodeGltfMaterialSources(*data, path, reader, materialSources, builder.mesh.materials,
        builder.mesh.embeddedImages, builder.mesh.materialWarning, false)) { return false; }
    mesh = std::move(builder.mesh);
    return true;
}

bool decodeGltfAnimation(std::string_view bytes, std::string_view path, const AssetReader& reader,
    AnimationData& animation, std::string& error) {
    GltfReadContext context{reader, error};
    const auto data = loadGltf(bytes, path, context);
    if (!data) { return false; }
    AnimationData candidate;
    std::vector<std::vector<std::uint16_t>> skinSlots;
    if (!buildAnimationJoints(*data, candidate, skinSlots, error) || !readAnimationClips(*data, candidate, error)) { return false; }
    animation = std::move(candidate);
    return true;
}

bool gltfMaterialNames(std::string_view bytes, std::vector<std::string>& names, std::string& error,
    std::vector<std::string>* textures) {
    const auto data = parseGltf(bytes, {}, error);
    if (!data) { return false; }
    std::vector<std::string> candidate;
    candidate.reserve(data->materials_count);
    for (std::size_t i = 0; i < data->materials_count; ++i) {
        auto* name = data->materials[i].name;
        if (name && *name) { candidate.emplace_back(name, cgltf_decode_string(name)); }
        else { candidate.push_back("Material " + std::to_string(i + 1)); }
    }
    if (textures) {
        std::vector<std::string> labels;
        labels.reserve(data->textures_count);
        std::unordered_map<const cgltf_image*, std::string> imageLabels;
        for (std::size_t i = 0; i < data->textures_count; ++i) {
            const auto& texture = data->textures[i];
            if (auto* name = texture.name; name && *name) {
                labels.emplace_back(name, cgltf_decode_string(name));
                continue;
            }
            const auto* image = texture.image;
            if (!image) { image = texture.basisu_image ? texture.basisu_image : texture.webp_image; }
            if (image) {
                // Multiple textures can share an image. cgltf decodes its strings in place.
                auto [entry, inserted] = imageLabels.try_emplace(image);
                if (inserted) {
                    auto& label = entry->second;
                    if (auto* name = image->name; name && *name) {
                        label.assign(name, cgltf_decode_string(name));
                    } else if (auto* uri = image->uri; uri && *uri) {
                        std::string_view file(uri, cgltf_decode_string(uri));
                        if (!file.starts_with("data:")) {
                            file = file.substr(0, file.find_first_of("?#"));
                            label.assign(file.substr(file.find_last_of('/') + 1));
                            label.resize(cgltf_decode_uri(label.data()));
                        }
                    }
                }
                if (!entry->second.empty()) {
                    labels.push_back(entry->second);
                    continue;
                }
            }
            labels.push_back("Texture " + std::to_string(i + 1));
        }
        *textures = std::move(labels);
    }
    names = std::move(candidate);
    return true;
}

namespace {
bool readEmbeddedImage(const cgltf_image& image, std::string_view uri, std::string_view& mime,
    std::string& bytes, std::string& error) {
    if (image.buffer_view) {
        const auto& view = *image.buffer_view;
        mime = image.mime_type ? image.mime_type : "";
        bytes.assign(reinterpret_cast<const char*>(cgltf_buffer_view_data(&view)), view.size);
        return true;
    }
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64")) {
        error = "Embedded glTF image must use a base64 data URI";
        return false;
    }
    mime = uri.substr(5, comma - 5 - std::string_view(";base64").size());
    const auto encoded = uri.substr(comma + 1);
    const auto padding = encoded.ends_with("==") ? 2u : encoded.ends_with('=') ? 1u : 0u;
    if (encoded.empty() || encoded.size() % 4 != 0) {
        error = "Invalid base64 glTF image";
        return false;
    }
    const auto size = encoded.size() / 4 * 3 - padding;
    const cgltf_options options{};
    void* raw = nullptr;
    if (cgltf_load_buffer_base64(&options, size, encoded.data(), &raw) != cgltf_result_success) {
        error = "Invalid base64 glTF image";
        return false;
    }
    const std::unique_ptr<void, decltype(&std::free)> decoded(raw, std::free);
    bytes.assign(static_cast<const char*>(decoded.get()), size);
    return true;
}

MaterialData gltfMaterialFactors(const cgltf_material& source) {
    MaterialData candidate;
    candidate.metallic = 1.0f;
    candidate.roughness = 1.0f;
    if (source.has_pbr_metallic_roughness) {
        const auto& pbr = source.pbr_metallic_roughness;
        std::copy_n(pbr.base_color_factor, 4, candidate.baseColor);
        candidate.metallic = pbr.metallic_factor;
        candidate.roughness = pbr.roughness_factor;
    } else if (source.has_pbr_specular_glossiness) {
        const auto& pbr = source.pbr_specular_glossiness;
        std::copy_n(pbr.diffuse_factor, 4, candidate.baseColor);
        candidate.metallic = 0.0f;
        candidate.roughness = 1.0f - pbr.glossiness_factor;
    }
    if (source.unlit) { candidate.shader = MaterialShader::Unlit; }
    if (source.occlusion_texture.texture) { candidate.occlusionStrength = source.occlusion_texture.scale; }
    std::copy_n(source.emissive_factor, 3, candidate.emissive);
    if (source.normal_texture.texture) { candidate.normalScale = source.normal_texture.scale; }
    if (source.alpha_mode == cgltf_alpha_mode_mask) { candidate.alphaMode = AlphaMode::Mask; }
    else if (source.alpha_mode == cgltf_alpha_mode_blend) { candidate.alphaMode = AlphaMode::Blend; }
    candidate.alphaCutoff = source.alpha_cutoff;
    candidate.doubleSided = source.double_sided != 0;
    if (source.has_clearcoat) {
        candidate.clearcoat = source.clearcoat.clearcoat_factor;
        candidate.clearcoatRoughness = source.clearcoat.clearcoat_roughness_factor;
        if (source.clearcoat.clearcoat_normal_texture.texture) {
            candidate.clearcoatNormalScale = source.clearcoat.clearcoat_normal_texture.scale;
        }
    }
    if (source.has_transmission) { candidate.transmission = source.transmission.transmission_factor; }
    if (source.has_ior) { candidate.ior = source.ior.ior; }
    if (source.has_volume) {
        candidate.thickness = source.volume.thickness_factor;
        candidate.attenuationDistance = source.volume.attenuation_distance == std::numeric_limits<float>::max() ?
            0.0f : source.volume.attenuation_distance;
        std::copy_n(source.volume.attenuation_color, 3, candidate.attenuationColor);
    }

    return candidate;
}

MaterialWrap gltfWrap(cgltf_wrap_mode mode) {
    if (mode == cgltf_wrap_mode_clamp_to_edge) { return MaterialWrap::Clamp; }
    if (mode == cgltf_wrap_mode_mirrored_repeat) { return MaterialWrap::Mirror; }
    return MaterialWrap::Repeat;
}

MaterialTextureSampling gltfTextureSampling(const cgltf_texture_view& view, unsigned texCoord) {
    MaterialTextureSampling sampling;
    sampling.texCoord = texCoord;
    if (view.has_transform) {
        std::copy_n(view.transform.offset, 2, sampling.offset);
        std::copy_n(view.transform.scale, 2, sampling.scale);
        sampling.rotation = view.transform.rotation;
    }
    if (const auto* sampler = view.texture->sampler) {
        sampling.useTextureSampler = false;
        sampling.wrapU = gltfWrap(sampler->wrap_s);
        sampling.wrapV = gltfWrap(sampler->wrap_t);
        const auto minFilter = sampler->min_filter;
        sampling.linearMin = minFilter != cgltf_filter_type_nearest &&
            minFilter != cgltf_filter_type_nearest_mipmap_nearest && minFilter != cgltf_filter_type_nearest_mipmap_linear;
        sampling.linearMag = sampler->mag_filter != cgltf_filter_type_nearest;
        sampling.linearMip = minFilter == cgltf_filter_type_undefined ||
            minFilter == cgltf_filter_type_nearest_mipmap_linear || minFilter == cgltf_filter_type_linear_mipmap_linear;
        sampling.mipmaps = minFilter != cgltf_filter_type_nearest && minFilter != cgltf_filter_type_linear;
    }
    return sampling;
}

struct GltfMaterialImage {
    std::string_view uri;
    std::string path;
    bool embedded = false;
};


bool decodeGltfMaterialSources(const cgltf_data& data, std::string_view path, const AssetReader& reader,
    std::span<const cgltf_material* const> materialSources, std::span<MaterialData> materials,
    std::vector<GltfImage>& images, std::string& error, bool extracting) {
    // Extraction requires a complete material. Rendering keeps valid channels
    // available even when a model has an unsupported or unavailable texture.
    const auto report = [&](std::string message) {
        if (error.empty()) { error = std::move(message); }
        return extracting;
    };
    using ImageSources = std::unordered_map<const cgltf_image*, GltfMaterialImage>;
    ImageSources sources;
    struct TextureSlot {
        const cgltf_image* image;
        std::string* destination;
    };
    std::vector<TextureSlot> slots;

    for (std::size_t i = 0; i < materials.size(); ++i) {
        const auto& source = *materialSources[i];
        auto& material = materials[i];
        material = gltfMaterialFactors(source);
        const auto& pbr = source.pbr_metallic_roughness;
        const auto& baseTexture = !source.has_pbr_metallic_roughness && source.has_pbr_specular_glossiness ?
            source.pbr_specular_glossiness.diffuse_texture : pbr.base_color_texture;
        const std::pair<const cgltf_texture_view*, std::string*> textures[] = {
            {&baseTexture, &material.baseColorTexture},
            {&pbr.metallic_roughness_texture, &material.metallicRoughnessTexture},
            {&source.normal_texture, &material.normalTexture},
            {&source.emissive_texture, &material.emissiveTexture},
            {&source.occlusion_texture, &material.occlusionTexture},
            {&source.transmission.transmission_texture, &material.transmissionTexture},
            {&source.volume.thickness_texture, &material.thicknessTexture},
            {&source.clearcoat.clearcoat_texture, &material.clearcoatTexture},
            {&source.clearcoat.clearcoat_roughness_texture, &material.clearcoatRoughnessTexture},
            {&source.clearcoat.clearcoat_normal_texture, &material.clearcoatNormalTexture}
        };
        for (std::size_t slot = 0; slot < std::size(textures); ++slot) {
            const auto& [view, destination] = textures[slot];
            if (!view->texture) { continue; }
            const auto* image = view->texture->image;
            const int texCoord = view->has_transform && view->transform.has_texcoord ? view->transform.texcoord : view->texcoord;
            if (!image || texCoord < 0 || texCoord > 1) {
                if (report("glTF material " + std::to_string(cgltf_material_index(&data, &source) + 1) +
                    (!image ? " has a texture with no PNG or JPEG source" : " uses a UV set other than TEXCOORD_0 or TEXCOORD_1"))) {
                    return false;
                }
                continue;
            }
            material.textureSampling[slot] = gltfTextureSampling(*view, static_cast<unsigned>(texCoord));
            slots.push_back({image, destination});
            const auto [found, inserted] = sources.try_emplace(image);
            if (!inserted) { continue; }
            auto& imageSource = found->second;
            if (auto* uri = image->uri) {
                imageSource.uri = std::string_view(uri, cgltf_decode_string(uri));
            }
            imageSource.embedded = image->buffer_view || imageSource.uri.starts_with("data:");
        }
    }

    const auto folder = gltfAssetFolder(path);
    const auto stem = path.substr(0, path.find_last_of('.'));
    std::unordered_set<std::string> usedPaths;
    std::unordered_set<std::string_view> readPaths;
    std::string imageBytes, readError;
    // Resolve external paths first so an embedded image never aliases one.
    for (auto& [image, source] : sources) {
        if (source.embedded) { continue; }
        if (source.uri.find('\0') != std::string_view::npos) {
            if (report("Invalid glTF image URI")) { return false; }
            continue;
        }
        std::string imagePath;
        if (!resolveGltfUri(folder, std::string(source.uri), imagePath, readError)) {
            if (report(readError)) { return false; }
            continue;
        }
        if (!isTextureAssetPath(imagePath)) {
            if (report("glTF image must be a PNG or JPEG: " + imagePath)) { return false; }
            continue;
        }
        source.path = std::move(imagePath);
        usedPaths.insert(extracting ? folded(source.path) : source.path);
        if (extracting && readPaths.insert(source.path).second) {
            readError.clear();
            if (!reader || reader(source.path, imageBytes, readError) != AssetReadResult::Found || imageBytes.empty()) {
                error = "Cannot read glTF image " + source.path + (readError.empty() ? "" : ": " + readError);
                return false;
            }
        }
    }
    std::unordered_map<const cgltf_image*, char*> textureNames;
    if (extracting) {
        // The first named texture in source order names a shared image.
        for (std::size_t i = 0; i < data.textures_count; ++i) {
            const auto& texture = data.textures[i];
            if (texture.image && texture.name && *texture.name) {
                const auto found = sources.find(texture.image);
                if (found != sources.end() && found->second.embedded) {
                    textureNames.try_emplace(texture.image, texture.name);
                }
            }
        }
    }
    const auto stemFromName = [](char* name) {
        if (!name || !*name) { return std::string{}; }
        std::string decoded(name, cgltf_decode_string(name));
        if (extensionIs(decoded, ".png") || extensionIs(decoded, ".jpg") || extensionIs(decoded, ".jpeg")) {
            decoded.resize(decoded.find_last_of('.'));
        }
        return safeAssetStem(std::move(decoded));
    };
    std::size_t embeddedSize = 0;
    for (std::size_t i = 0; i < data.images_count; ++i) {
        const auto* image = &data.images[i];
        const auto found = sources.find(image);
        if (found == sources.end() || !found->second.embedded) { continue; }
        auto& source = found->second;
        if (source.uri.find('\0') != std::string_view::npos) {
            if (report("Invalid glTF image URI")) { return false; }
            continue;
        }
        std::string_view mime;
        std::string embeddedBytes;
        if (!readEmbeddedImage(*image, source.uri, mime, embeddedBytes, readError)) {
            if (report(readError)) { return false; }
            continue;
        }
        if (embeddedBytes.empty() || (mime != "image/png" && mime != "image/jpeg")) {
            if (report("Embedded glTF image must be a PNG or JPEG")) { return false; }
            continue;
        }
        // Buffer views can overlap, so bound the total copied image payload.
        constexpr std::size_t kMaxEmbeddedImages = 2 * kMaxAssetBytes;
        if (embeddedBytes.size() > kMaxEmbeddedImages - embeddedSize) {
            if (report("Embedded glTF images exceed 128 MiB")) { return false; }
            continue;
        }
        embeddedSize += embeddedBytes.size();
        std::string imageStem;
        if (extracting) {
            const auto named = textureNames.find(image);
            if (named != textureNames.end()) { imageStem = stemFromName(named->second); }
            if (imageStem.empty()) { imageStem = stemFromName(image->name); }
        }
        if (imageStem.empty()) { imageStem = std::string(stem) + "_" + std::to_string(i); }
        else { imageStem.insert(0, folder); }
        const auto* extension = mime == "image/png" ? ".png" : ".jpg";
        auto imagePath = imageStem + extension;
        std::size_t suffix = 0;
        while (!usedPaths.insert(extracting ? folded(imagePath) : imagePath).second) {
            imagePath = imageStem + "_" + std::to_string(++suffix) + extension;
        }
        if (!isTextureAssetPath(imagePath)) {
            if (report("Invalid destination for embedded glTF image")) { return false; }
            continue;
        }
        source.path = std::move(imagePath);
        images.push_back({source.path, std::move(embeddedBytes)});
    }
    for (const auto& slot : slots) { *slot.destination = sources.find(slot.image)->second.path; }
    for (auto& material : materials) {
        if (extracting) {
            if (!decodeMaterial(encodeMaterial(material), material, error)) { return false; }
            continue;
        }
        const auto finite = [&](float value, float fallback) {
            if (std::isfinite(value)) { return value; }
            (void)report("Nonfinite glTF material parameter");
            return fallback;
        };
        const auto channel = [&](float value, float fallback) { return std::clamp(finite(value, fallback), 0.0f, 1.0f); };
        for (float& value : material.baseColor) { value = channel(value, 1.0f); }
        for (float& value : material.emissive) { value = channel(value, 0.0f); }
        material.metallic = channel(material.metallic, 1.0f);
        material.roughness = channel(material.roughness, 1.0f);
        material.normalScale = finite(material.normalScale, 1.0f);
        material.alphaCutoff = std::max(finite(material.alphaCutoff, 0.5f), 0.0f);
        material.occlusionStrength = channel(material.occlusionStrength, 1.0f);
        material.transmission = channel(material.transmission, 0.0f);
        material.clearcoat = channel(material.clearcoat, 0.0f);
        material.clearcoatRoughness = channel(material.clearcoatRoughness, 0.0f);
        material.clearcoatNormalScale = finite(material.clearcoatNormalScale, 1.0f);
        material.ior = finite(material.ior, 1.5f);
        if (material.ior != 0.0f && material.ior < 1.0f) { material.ior = 1.5f; }
        material.thickness = std::max(finite(material.thickness, 0.0f), 0.0f);
        material.attenuationDistance = std::max(finite(material.attenuationDistance, 0.0f), 0.0f);
        for (float& value : material.attenuationColor) { value = channel(value, 1.0f); }
    }
    return true;
}
} // namespace

bool decodeGltfMaterial(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::size_t materialIndex, MaterialData& material, std::vector<GltfImage>& embedded, std::string& error) {
    GltfReadContext context{reader, error};
    const auto data = loadGltf(bytes, path, context);
    if (!data) { return false; }
    if (materialIndex >= data->materials_count) { error = "glTF material index is out of range"; return false; }
    MaterialData candidate;
    std::vector<GltfImage> images;
    const auto* source = &data->materials[materialIndex];
    if (!decodeGltfMaterialSources(*data, path, reader, {&source, 1}, {&candidate, 1}, images, error, true)) { return false; }
    material = std::move(candidate);
    embedded = std::move(images);
    return true;
}

bool decodeGltfMaterials(std::string_view bytes, std::string_view path, const AssetReader& reader,
    std::vector<MaterialData>& materials, std::vector<GltfImage>& embedded, std::string& error) {
    GltfReadContext context{reader, error};
    const auto data = loadGltf(bytes, path, context);
    if (!data) { return false; }
    std::vector<MaterialData> candidates(data->materials_count);
    std::vector<GltfImage> images;
    std::vector<const cgltf_material*> sources;
    sources.reserve(data->materials_count);
    for (std::size_t i = 0; i < data->materials_count; ++i) { sources.push_back(&data->materials[i]); }
    if (!decodeGltfMaterialSources(*data, path, reader, sources, candidates, images, error, true)) { return false; }
    materials = std::move(candidates);
    embedded = std::move(images);
    return true;
}
} // namespace sat
