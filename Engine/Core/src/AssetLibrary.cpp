#include "AssetLibrary.hpp"

#include "AssetImporter.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <system_error>
#include <utility>

namespace sat {
namespace {
std::size_t cost(const MeshData& mesh) {
    return mesh.vertices.size() * sizeof(MeshVertex) + mesh.indices.size() * sizeof(std::uint32_t);
}
std::size_t cost(const TextureData& texture) {
    std::size_t bytes = texture.pixels.size();
    for (const auto& mip : texture.mips) { bytes += mip.pixels.size(); }
    return bytes;
}
}
namespace {
AssetReader directoryReader(const std::filesystem::path& projectRoot, bool shader) {
    return [root = projectRoot, shader](std::string_view path, std::string& bytes, std::string& error) {
        error.clear();
        bytes.clear();
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
        std::ifstream file(candidate, std::ios::binary | std::ios::ate);
        if (!file) { error = "Cannot open asset"; return AssetReadResult::Error; }
        const auto size = file.tellg();
        const std::size_t limit = shader ? 1024 * 1024 : metadata ? 4096 :
            isMaterialAssetPath(assetPath) ? 8192 : isShaderAssetPath(assetPath) ? 1024 * 1024 : kMaxAssetBytes;
        if (size < 0 || size > static_cast<std::streamoff>(limit)) { error = "Asset exceeds read limit"; return AssetReadResult::Error; }
        bytes.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) { error = "Failed reading asset"; return AssetReadResult::Error; }
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
void AssetLibrary::setReader(AssetReader reader) { clear(); m_reader = std::move(reader); }
void AssetLibrary::clear() {
    m_meshes.clear(); m_textures.clear(); m_materials.clear(); m_errors.clear(); m_cachedBytes = 0; m_limitReported = false;
}
void AssetLibrary::invalidateMaterial(std::string_view path) {
    if (const auto found = m_materials.find(path); found != m_materials.end()) { m_materials.erase(found); }
    std::erase_if(m_errors, [&](const std::string& message) {
        return message.starts_with(path) && std::string_view(message).substr(path.size(), 2) == ": ";
    });
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
bool AssetLibrary::stage(std::string_view path, const AssetImportSettings* overrides,
    PreparedAssetImport& prepared, std::string& error) {
    error.clear();
    const bool isMesh = isMeshAssetPath(path);
    if (!isMesh && !isTextureAssetPath(path)) { error = "Unsupported or invalid asset path"; return false; }
    if (!m_reader) { error = "No asset source configured"; return false; }
    const bool present = isMesh ? m_meshes.contains(path) : m_textures.contains(path);
    if (!present && m_meshes.size() + m_textures.size() + m_materials.size() >= 4096) { error = "Asset cache limit exceeded (4096 paths)"; return false; }
    PreparedAssetImport candidate;
    candidate.path = path;
    candidate.isMesh = isMesh;
    candidate.reservedNew = !present;
    std::string bytes;
    if (overrides) { candidate.settings = *overrides; }
    else {
        const auto result = m_reader(std::string(path) + ".satimport", bytes, error);
        if (result == AssetReadResult::Error) { return false; }
        if (result == AssetReadResult::Found && !parseImportSettings(bytes, candidate.settings, error)) { return false; }
    }
    bytes.clear();
    error.clear();
    const auto result = m_reader(path, bytes, error);
    if (result != AssetReadResult::Found) {
        if (error.empty()) { error = result == AssetReadResult::Missing ? "Asset not found" : "Asset reader failed"; }
        return false;
    }
    if (bytes.size() > kMaxAssetBytes) { error = "Asset exceeds 64 MiB"; return false; }
    if (isMesh ? !importMesh(bytes, candidate.settings, candidate.mesh, error) :
                 !importTexture(bytes, candidate.settings, candidate.texture, error)) { return false; }
    std::size_t previousBytes = 0;
    if (isMesh) {
        const auto found = m_meshes.find(path);
        if (found != m_meshes.end()) { previousBytes = cost(found->second.data); }
    } else {
        const auto found = m_textures.find(path);
        if (found != m_textures.end()) { previousBytes = cost(found->second.data); }
    }
    constexpr std::size_t kMaxCacheBytes = 256 * 1024 * 1024;
    const auto newBytes = isMesh ? cost(candidate.mesh) : cost(candidate.texture);
    if (newBytes > kMaxCacheBytes - (m_cachedBytes - previousBytes)) { error = "Decoded asset cache exceeds 256 MiB; reload assets to clear it"; return false; }
    if (isMesh) { m_meshes.try_emplace(candidate.path); }
    else { m_textures.try_emplace(candidate.path); }
    prepared = std::move(candidate);
    return true;
}
void AssetLibrary::discard(PreparedAssetImport& prepared) {
    if (prepared.reservedNew) {
        if (prepared.isMesh) { m_meshes.erase(prepared.path); }
        else { m_textures.erase(prepared.path); }
    }
    prepared.reservedNew = false;
    prepared.path.clear();
}
void AssetLibrary::commit(PreparedAssetImport&& prepared) {
    assert(!prepared.path.empty());
    if (prepared.isMesh) {
        assert(m_meshes.contains(prepared.path));
        auto& entry = m_meshes.find(prepared.path)->second;
        m_cachedBytes -= cost(entry.data);
        entry.data = std::move(prepared.mesh);
        m_cachedBytes += cost(entry.data);
        entry.valid = true;
    } else {
        assert(m_textures.contains(prepared.path));
        auto& entry = m_textures.find(prepared.path)->second;
        m_cachedBytes -= cost(entry.data);
        entry.data = std::move(prepared.texture);
        m_cachedBytes += cost(entry.data);
        entry.valid = true;
    }
    prepared.reservedNew = false;
    std::erase_if(m_errors, [&](const std::string& message) {
        return message.starts_with(prepared.path) && std::string_view(message).substr(prepared.path.size(), 2) == ": ";
    });
    prepared.path.clear();
}
const MeshData* AssetLibrary::mesh(std::string_view path) {
    if (const auto found = m_meshes.find(path); found != m_meshes.end()) { return found->second.valid ? &found->second.data : nullptr; }
    PreparedAssetImport prepared;
    std::string error;
    if (!isMeshAssetPath(path)) { error = "Unsupported or invalid mesh asset path"; }
    else if (stage(path, nullptr, prepared, error)) { commit(std::move(prepared)); return &m_meshes.find(path)->second.data; }
    if (m_meshes.size() + m_textures.size() + m_materials.size() < 4096) { m_meshes.try_emplace(std::string(path)); m_errors.push_back(std::string(path) + ": " + error); }
    else if (!m_limitReported) { m_errors.emplace_back("Asset cache limit exceeded (4096 paths)"); m_limitReported = true; }
    return nullptr;
}
const TextureData* AssetLibrary::texture(std::string_view path) {
    if (const auto found = m_textures.find(path); found != m_textures.end()) { return found->second.valid ? &found->second.data : nullptr; }
    PreparedAssetImport prepared;
    std::string error;
    if (!isTextureAssetPath(path)) { error = "Unsupported or invalid texture asset path"; }
    else if (stage(path, nullptr, prepared, error)) { commit(std::move(prepared)); return &m_textures.find(path)->second.data; }
    if (m_meshes.size() + m_textures.size() + m_materials.size() < 4096) { m_textures.try_emplace(std::string(path)); m_errors.push_back(std::string(path) + ": " + error); }
    else if (!m_limitReported) { m_errors.emplace_back("Asset cache limit exceeded (4096 paths)"); m_limitReported = true; }
    return nullptr;
}
}
