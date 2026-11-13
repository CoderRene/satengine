#include "AssetData.hpp"

#include "Math.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

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
    return isValidAssetPath(path) && extensionIs(path, ".obj");
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
                    candidate.vertices.push_back({p[0], p[1], p[2], n.x, n.y, n.z, tex[0], tex[1], 0, 0, 0, 1});
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
                const auto& a = candidate.vertices[face[0]];
                const auto& b = candidate.vertices[face[i]];
                const auto& c = candidate.vertices[face[i + 1]];
                const Vec3 ab = positionOf(face[i]) - positionOf(face[0]);
                const Vec3 ac = positionOf(face[i + 1]) - positionOf(face[0]);
                const float det = (b.u - a.u) * (c.v - a.v) - (c.u - a.u) * (b.v - a.v);
                if (std::abs(det) > 1e-8f) {
                    const Vec3 tangent = (ab * (c.v - a.v) - ac * (b.v - a.v)) * (1.0f / det);
                    const Vec3 bitangent = (ac * (b.u - a.u) - ab * (c.u - a.u)) * (1.0f / det);
                    for (const auto vertexIndex : {face[0], face[i], face[i + 1]}) {
                        auto& v = candidate.vertices[vertexIndex];
                        v.tx += tangent.x; v.ty += tangent.y; v.tz += tangent.z;
                        bitangents[vertexIndex] = bitangents[vertexIndex] + bitangent;
                    }
                }
                if (rightHanded) { candidate.indices.insert(candidate.indices.end(), {face[0], face[i + 1], face[i]}); }
                else { candidate.indices.insert(candidate.indices.end(), {face[0], face[i], face[i + 1]}); }
            }
        } else if (kind != "o" && kind != "g" && kind != "s" && kind != "mtllib" && kind != "usemtl") {
            return fail("Unsupported record (static polygon OBJ required)");
        }
    }
    if (candidate.indices.empty()) { return fail("No triangles found"); }
    candidate.minimum[0] = candidate.maximum[0] = candidate.vertices[0].x;
    candidate.minimum[1] = candidate.maximum[1] = candidate.vertices[0].y;
    candidate.minimum[2] = candidate.maximum[2] = candidate.vertices[0].z;
    for (std::size_t i = 0; i < candidate.vertices.size(); ++i) {
        auto& v = candidate.vertices[i];
        const Vec3 n{v.nx, v.ny, v.nz};
        Vec3 t{v.tx, v.ty, v.tz};
        t = t - n * dot(t, n);
        if (!(dot(t, t) > 1e-12f) || !std::isfinite(dot(t, t))) {
            t = cross(std::abs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, n);
        }
        t = normalize(t);
        v.tx = t.x; v.ty = t.y; v.tz = t.z;
        v.tw = dot(cross(n, t), bitangents[i]) < 0 ? -1.0f : 1.0f;
        const float coordinates[3] = {v.x, v.y, v.z};
        for (int axis = 0; axis < 3; ++axis) {
            candidate.minimum[axis] = std::min(candidate.minimum[axis], coordinates[axis]);
            candidate.maximum[axis] = std::max(candidate.maximum[axis], coordinates[axis]);
        }
    }
    mesh = std::move(candidate);
    return true;
}
} // namespace sat
