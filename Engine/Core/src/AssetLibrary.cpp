#include "AssetLibrary.hpp"

#include "AssetImporter.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <system_error>
#include <utility>

namespace sat {
namespace {
constexpr std::size_t kMaxCacheBytes = 256 * 1024 * 1024;

std::size_t cost(const MeshData& mesh) {
    auto bytes = mesh.vertices.size() * sizeof(MeshVertex) + mesh.indices.size() * sizeof(std::uint32_t) +
        mesh.sections.size() * sizeof(MeshSection) + mesh.materials.size() * sizeof(MaterialData) + mesh.materialWarning.size();
    bytes += mesh.nodes.size() * sizeof(MeshNode) + mesh.nodeSections.size() * sizeof(MeshSection) +
        mesh.nodeIndices.size() * sizeof(std::uint32_t);
    bytes += mesh.skinVertices.size() * sizeof(MeshSkinVertex) + mesh.jointBounds.size() * sizeof(AnimationBounds) + mesh.restPalette.size() * sizeof(Mat4);
    for (const auto& node : mesh.nodes) { bytes += node.name.size(); }
    for (const auto& material : mesh.materials) {
        bytes += material.baseColorTexture.size() + material.metallicRoughnessTexture.size() +
            material.normalTexture.size() + material.emissiveTexture.size() + material.occlusionTexture.size() +
            material.transmissionTexture.size() + material.thicknessTexture.size() + material.clearcoatTexture.size() +
            material.clearcoatRoughnessTexture.size() + material.clearcoatNormalTexture.size() + material.shaderPath.size();
    }
    for (const auto& image : mesh.embeddedImages) { bytes += sizeof(GltfImage) + image.path.size() + image.bytes.size(); }
    for (const auto& name : mesh.materialNames) { bytes += sizeof(std::string) + name.size(); }
    for (const auto& path : mesh.dependencies) { bytes += sizeof(std::string) + path.size(); }
    return bytes;
}
std::size_t cost(const TextureData& texture) {
    std::size_t bytes = texture.pixels.size();
    for (const auto& mip : texture.mips) { bytes += mip.pixels.size(); }
    return bytes;
}
}
namespace {
AssetReadResult openAssetFile(const std::filesystem::path& root, std::string_view path, bool shader,
    std::ifstream& file, std::streamoff& size, std::string& error) {
    const bool metadata = path.ends_with(".satimport");
    const auto assetPath = metadata ? path.substr(0, path.size() - std::string_view(".satimport").size()) : path;
    const bool validShader = path.starts_with("Shaders/") && path.size() > 8 &&
        path.substr(8).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.") ==
            std::string_view::npos && path.substr(8) != "." && path.substr(8) != "..";
    if (!(shader ? validShader : isValidAssetPath(assetPath)) || root.empty()) {
        error = shader ? "Invalid shader path" : "Asset path must be relative to the open project's Assets/ folder";
        return AssetReadResult::Error;
    }
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::canonical(root, ec);
    if (ec) { error = "Cannot resolve project root: " + ec.message(); return AssetReadResult::Error; }
    const auto source = canonicalRoot / std::filesystem::path(std::u8string(path.begin(), path.end()));
    const auto candidate = std::filesystem::canonical(source, ec);
    if (ec == std::errc::no_such_file_or_directory) {
        // A dangling sidecar symlink is an error, not absent metadata.
        std::error_code statusError;
        const auto status = std::filesystem::symlink_status(source, statusError);
        if (!statusError && std::filesystem::is_symlink(status)) { error = "Dangling asset symlink"; return AssetReadResult::Error; }
        return AssetReadResult::Missing;
    }
    if (ec) { error = "Cannot resolve asset: " + ec.message(); return AssetReadResult::Error; }
    const auto inside = std::mismatch(canonicalRoot.begin(), canonicalRoot.end(), candidate.begin(), candidate.end());
    if (inside.first != canonicalRoot.end()) { error = "Asset resolves outside the project root"; return AssetReadResult::Error; }
    if (!std::filesystem::is_regular_file(candidate, ec) || ec) { error = "Asset is not a readable regular file"; return AssetReadResult::Error; }
    file.open(candidate, std::ios::binary | std::ios::ate);
    if (!file) { error = "Cannot open asset"; return AssetReadResult::Error; }
    size = file.tellg();
    const std::size_t limit = shader ? 1024 * 1024 : metadata ? 4096 :
        isMaterialAssetPath(assetPath) ? 8192 : isShaderAssetPath(assetPath) ? 1024 * 1024 : kMaxAssetBytes;
    if (size < 0 || size > static_cast<std::streamoff>(limit)) { error = "Asset exceeds read limit"; return AssetReadResult::Error; }
    file.seekg(0);
    if (!file) { error = "Cannot seek asset"; return AssetReadResult::Error; }
    return AssetReadResult::Found;
}

AssetReader directoryReader(const std::filesystem::path& projectRoot, bool shader) {
    return [root = projectRoot, shader](std::string_view path, std::string& bytes, std::string& error) {
        error.clear();
        bytes.clear();
        std::ifstream file;
        std::streamoff size = 0;
        const auto result = openAssetFile(root, path, shader, file, size, error);
        if (result != AssetReadResult::Found) { return result; }
        bytes.resize(static_cast<std::size_t>(size));
        if (!file.read(bytes.data(), static_cast<std::streamsize>(size))) { error = "Failed reading asset"; return AssetReadResult::Error; }
        return AssetReadResult::Found;
    };
}
} // namespace
AssetReader fileAssetReader(const std::filesystem::path& projectRoot) {
    return directoryReader(projectRoot, false);
}
AssetReader fileShaderReader(const std::filesystem::path& executableRoot) {
    return directoryReader(executableRoot, true);
}
bool inspectTextureFile(const std::filesystem::path& root, std::string_view path,
    const AssetImportSettings& settings, TextureInfo& info, std::string& error) {
    error.clear();
    if (!isTextureAssetPath(path)) { error = "Unsupported or invalid texture asset path"; return false; }
    std::ifstream file;
    std::streamoff size = 0;
    const auto result = openAssetFile(root, path, false, file, size, error);
    if (result != AssetReadResult::Found) {
        if (result == AssetReadResult::Missing) { error = "Asset not found"; }
        return false;
    }
    int width = 0;
    int height = 0;
    if (!readTextureDimensions(file, width, height, error)) { return false; }
    info = textureImportInfo(width, height, settings);
    return true;
}
void AssetLibrary::setReader(AssetReader reader) { clear(); m_reader = std::move(reader); }
void AssetLibrary::clear() {
    m_recency.clear();
    m_meshes.clear(); m_textures.clear(); m_materials.clear(); m_errors.clear(); m_cachedBytes = 0; m_limitReported = false;
}
void AssetLibrary::invalidateMaterial(std::string_view path) {
    if (const auto found = m_materials.find(path); found != m_materials.end()) { m_materials.erase(found); }
    std::erase_if(m_errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
}
void AssetLibrary::invalidateAsset(std::string_view path) {
    const auto eraseDecoded = [&](auto& cache, auto entry) {
        if (entry->second.valid) {
            m_cachedBytes -= cost(entry->second.data);
            m_recency.erase(entry->second.recency);
        }
        return cache.erase(entry);
    };
    if (const auto found = m_textures.find(path); found != m_textures.end()) {
        eraseDecoded(m_textures, found);
    }
    for (auto it = m_meshes.begin(); it != m_meshes.end();) {
        const auto& dependencies = it->second.data.dependencies;
        if (it->first == path || std::ranges::find(dependencies, path) != dependencies.end()) {
            it = eraseDecoded(m_meshes, it);
        } else {
            ++it;
        }
    }
    invalidateMaterial(path);
}
const MaterialData* AssetLibrary::material(std::string_view path) {
    if (const auto found = m_materials.find(path); found != m_materials.end()) {
        return found->second.valid ? &found->second.data : nullptr;
    }
    if (m_meshes.size() + m_textures.size() + m_materials.size() >= 4096) {
        if (!m_limitReported) { m_errors.emplace_back("Asset cache limit exceeded (4096 paths)"); m_limitReported = true; }
        return nullptr;
    }
    auto& entry = m_materials.try_emplace(std::string(path)).first->second;
    std::string bytes, error;
    entry.valid = isMaterialAssetPath(path) && m_reader &&
        m_reader(path, bytes, error) == AssetReadResult::Found && decodeMaterial(bytes, entry.data, error);
    if (!entry.valid) { m_errors.push_back(std::string(path) + ": " + (error.empty() ? "Cannot read material" : error)); }
    return entry.valid ? &entry.data : nullptr;
}
bool decodeAssetImport(const AssetReader& reader, std::string_view path, const AssetImportSettings* overrides,
    PreparedAssetImport& prepared, std::string& error) {
    error.clear();
    const bool isMesh = isMeshAssetPath(path);
    if (!isMesh && !isTextureAssetPath(path)) { error = "Unsupported or invalid asset path"; return false; }
    if (!reader) { error = "No asset source configured"; return false; }
    PreparedAssetImport candidate;
    candidate.path = path;
    candidate.isMesh = isMesh;
    std::string bytes;
    if (overrides) { candidate.settings = *overrides; }
    else {
        const auto result = reader(std::string(path) + ".satimport", bytes, error);
        if (result == AssetReadResult::Error) { return false; }
        if (result == AssetReadResult::Found && !parseImportSettings(bytes, candidate.settings, error)) { return false; }
    }
    bytes.clear();
    error.clear();
    const auto result = reader(path, bytes, error);
    if (result != AssetReadResult::Found) {
        if (error.empty()) { error = result == AssetReadResult::Missing ? "Asset not found" : "Asset reader failed"; }
        return false;
    }
    if (bytes.size() > kMaxAssetBytes) { error = "Asset exceeds 64 MiB"; return false; }
    if (isMesh) {
        std::vector<std::string> dependencies;
        const AssetReader readDependency = [&](std::string_view dependency, std::string& output, std::string& readError) {
            if (std::ranges::find(dependencies, dependency) == dependencies.end()) {
                dependencies.emplace_back(dependency);
            }
            return reader(dependency, output, readError);
        };
        if (!importMesh(path, bytes, readDependency, candidate.settings, candidate.mesh, error)) { return false; }
        candidate.mesh.dependencies = std::move(dependencies);
    } else if (!importTexture(bytes, candidate.settings, candidate.texture, error)) {
        return false;
    }
    prepared = std::move(candidate);
    return true;
}

bool AssetLibrary::hasSpaceFor(std::string_view path, bool isMesh) const {
    return (isMesh ? m_meshes.contains(path) : m_textures.contains(path)) ||
        m_meshes.size() + m_textures.size() + m_materials.size() < 4096;
}

bool AssetLibrary::stage(std::string_view path, const AssetImportSettings* overrides,
    PreparedAssetImport& prepared, std::string& error) {
    if (!hasSpaceFor(path, isMeshAssetPath(path))) { error = "Asset cache limit exceeded (4096 paths)"; return false; }
    PreparedAssetImport candidate;
    if (!decodeAssetImport(m_reader, path, overrides, candidate, error) || !reserveImport(candidate, error)) { return false; }
    prepared = std::move(candidate);
    return true;
}

bool AssetLibrary::reserveImport(PreparedAssetImport& prepared, std::string& error) {
    if (prepared.path.empty()) { error = "No decoded asset to publish"; return false; }
    if (!hasSpaceFor(prepared.path, prepared.isMesh)) { error = "Asset cache limit exceeded (4096 paths)"; return false; }
    prepared.reservedNew = prepared.isMesh ? !m_meshes.contains(prepared.path) : !m_textures.contains(prepared.path);
    const auto newBytes = prepared.isMesh ? cost(prepared.mesh) : cost(prepared.texture);
    if (newBytes > kMaxCacheBytes) { error = "Decoded asset exceeds 256 MiB"; return false; }
    if (prepared.isMesh) { reserveDecoded(m_meshes, prepared); }
    else { reserveDecoded(m_textures, prepared); }
    return true;
}

template<typename T>
void AssetLibrary::reserveDecoded(Cache<DecodedEntry<T>>& cache, const PreparedAssetImport& prepared) {
    const auto [found, inserted] = cache.try_emplace(prepared.path);
    auto& entry = found->second;
    if (entry.valid) { return; }
    try {
        entry.recency = m_recency.emplace(m_recency.begin(), found->first);
    } catch (...) {
        if (inserted) { cache.erase(found); }
        throw;
    }
}

template<typename T>
void AssetLibrary::discardDecoded(Cache<DecodedEntry<T>>& cache, const PreparedAssetImport& prepared) {
    const auto found = cache.find(prepared.path);
    assert(found != cache.end());
    if (!found->second.valid) { m_recency.erase(found->second.recency); }
    if (prepared.reservedNew) { cache.erase(found); }
}

void AssetLibrary::discard(PreparedAssetImport& prepared) {
    if (prepared.path.empty()) { return; }
    if (prepared.isMesh) { discardDecoded(m_meshes, prepared); }
    else { discardDecoded(m_textures, prepared); }
    prepared.reservedNew = false;
    prepared.path.clear();
}

void AssetLibrary::evictOldest() {
    const auto path = m_recency.back();
    m_recency.pop_back();
    if (const auto found = m_meshes.find(path); found != m_meshes.end() && found->second.valid) {
        m_cachedBytes -= cost(found->second.data);
        m_meshes.erase(found);
    } else {
        const auto texture = m_textures.find(path);
        assert(texture != m_textures.end() && texture->second.valid);
        m_cachedBytes -= cost(texture->second.data);
        m_textures.erase(texture);
    }
}

template<typename T>
void AssetLibrary::commitDecoded(DecodedEntry<T>& entry, T&& data) {
    // Keep the replacement out of the eviction candidates while making room.
    m_recency.splice(m_recency.begin(), m_recency, entry.recency);
    m_cachedBytes -= cost(entry.data);
    const auto newBytes = cost(data);
    while (newBytes > kMaxCacheBytes - m_cachedBytes) { evictOldest(); }
    entry.data = std::move(data);
    m_cachedBytes += newBytes;
    entry.valid = true;
}

void AssetLibrary::commit(PreparedAssetImport&& prepared) {
    assert(!prepared.path.empty());
    if (prepared.isMesh) {
        assert(m_meshes.contains(prepared.path));
        commitDecoded(m_meshes.find(prepared.path)->second, std::move(prepared.mesh));
    } else {
        assert(m_textures.contains(prepared.path));
        commitDecoded(m_textures.find(prepared.path)->second, std::move(prepared.texture));
    }
    prepared.reservedNew = false;
    std::erase_if(m_errors, [&](const std::string& message) {
        return message.starts_with(prepared.path) && std::string_view(message).substr(prepared.path.size(), 2) == ": ";
    });
    prepared.path.clear();
}
const MeshData* AssetLibrary::mesh(std::string_view path) {
    if (const auto found = m_meshes.find(path); found != m_meshes.end()) {
        auto& entry = found->second;
        if (!entry.valid) { return nullptr; }
        m_recency.splice(m_recency.begin(), m_recency, entry.recency);
        return &entry.data;
    }
    PreparedAssetImport prepared;
    std::string error;
    if (!isMeshAssetPath(path)) { error = "Unsupported or invalid mesh asset path"; }
    else if (stage(path, nullptr, prepared, error)) { commit(std::move(prepared)); return &m_meshes.find(path)->second.data; }
    if (m_meshes.size() + m_textures.size() + m_materials.size() < 4096) { m_meshes.try_emplace(std::string(path)); m_errors.push_back(std::string(path) + ": " + error); }
    else if (!m_limitReported) { m_errors.emplace_back("Asset cache limit exceeded (4096 paths)"); m_limitReported = true; }
    return nullptr;
}
const TextureData* AssetLibrary::texture(std::string_view path) {
    if (const auto found = m_textures.find(path); found != m_textures.end()) {
        auto& entry = found->second;
        if (!entry.valid) { return nullptr; }
        m_recency.splice(m_recency.begin(), m_recency, entry.recency);
        return &entry.data;
    }
    PreparedAssetImport prepared;
    std::string error;
    if (!isTextureAssetPath(path)) { error = "Unsupported or invalid texture asset path"; }
    else if (stage(path, nullptr, prepared, error)) { commit(std::move(prepared)); return &m_textures.find(path)->second.data; }
    if (m_meshes.size() + m_textures.size() + m_materials.size() < 4096) { m_textures.try_emplace(std::string(path)); m_errors.push_back(std::string(path) + ": " + error); }
    else if (!m_limitReported) { m_errors.emplace_back("Asset cache limit exceeded (4096 paths)"); m_limitReported = true; }
    return nullptr;
}
}
