#include "PakArchive.hpp"
#include "AssetLibrary.hpp"
#include "Scene.hpp"
#include "SceneFile.hpp"
#include "module/Pak/PakCrypto.hpp"
#include <lua.hpp>
#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int dump(lua_State*, const void* bytes, std::size_t size, void* target) {
    try { static_cast<std::string*>(target)->append(static_cast<const char*>(bytes), size); return 0; }
    catch (...) { return 1; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { throw std::runtime_error("Cannot read " + path.string()); }
    std::string result{std::istreambuf_iterator<char>(stream), {}};
    if (stream.bad()) { throw std::runtime_error("Read failed: " + path.string()); }
    return result;
}
}
#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        if (argc == 3 && std::filesystem::path(argv[1]) == "--test-keygen") {
            sat::ScopedPakKey key;
            if (!sat::pakRandom(key.bytes)) { throw std::runtime_error("Cannot generate test key"); }
            std::ofstream file(std::filesystem::path(argv[2]), std::ios::binary);
            file.write(reinterpret_cast<const char*>(key.bytes.data()), key.bytes.size()); file.close();
            if (!file) { throw std::runtime_error("Cannot write test key"); }
            return 0;
        }
        if (argc != 4) { throw std::runtime_error("Usage: SatPakBuilder project-root output.pak key.bin"); }
        const std::filesystem::path keyPath = argv[3];
        const bool grouped = std::filesystem::is_directory(keyPath);
        const std::filesystem::path root = argv[1];
        std::unique_ptr<lua_State, decltype(&lua_close)> state(luaL_newstate(), lua_close);
        if (!state) { throw std::runtime_error("Cannot allocate Lua compiler"); }
        std::vector<sat::PakEntry> entries{{"Scene.sat", read(root / "Scene.sat")}};
        sat::Scene scene;
        std::string sceneError;
        if (!sat::deserializeScene(entries.front().bytes, scene, sceneError)) {
            throw std::runtime_error("Cannot pack scene: " + sceneError);
        }
        sat::AssetLibrary assets;
        assets.setReader(sat::fileAssetReader(root));
        for (const auto& entity : scene.entities()) {
            if (!entity.hasMesh) { continue; }
            if ((!entity.mesh.meshPath.empty() && assets.mesh(entity.mesh.meshPath) == nullptr) ||
                (!entity.mesh.texturePath.empty() && assets.texture(entity.mesh.texturePath) == nullptr)) {
                throw std::runtime_error("Entity " + entity.name + ": " + assets.errors().back());
            }
        }
        std::vector<std::filesystem::path> files;
        for (const char* folder : {"Scripts", "Assets"}) {
            if (!std::filesystem::exists(root / folder)) { continue; }
            for (const auto& file : std::filesystem::recursive_directory_iterator(root / folder)) {
                if (file.is_symlink()) { throw std::runtime_error("Symlinks are not supported in pak inputs"); }
                if (file.is_regular_file() && (std::string_view(folder) == "Assets" || file.path().extension() == ".lua")) {
                    files.push_back(file.path());
                }
            }
        }
        std::sort(files.begin(), files.end());
        for (const auto& path : files) {
            const auto encoded = path.lexically_relative(root).generic_u8string();
            std::string name(encoded.begin(), encoded.end());
            if (name.ends_with(".satimport.tmp")) { continue; }
            if (name.ends_with(".satimport")) {
                const auto source = std::string_view(name).substr(0, name.size() - std::string_view(".satimport").size());
                if (!sat::isMeshAssetPath(source) && !sat::isTextureAssetPath(source)) {
                    throw std::runtime_error("Import metadata has no supported source: " + name);
                }
                const auto sourcePath = root / std::filesystem::path(std::u8string(source.begin(), source.end()));
                if (!std::filesystem::is_regular_file(sourcePath)) { throw std::runtime_error("Orphan import metadata: " + name); }
            }
            if ((sat::isMeshAssetPath(name) && assets.mesh(name) == nullptr) ||
                (sat::isTextureAssetPath(name) && assets.texture(name) == nullptr)) {
                throw std::runtime_error(assets.errors().back());
            }
            std::string bytes = read(path);
            if (name.starts_with("Scripts/")) {
                const std::string label = "@" + name;
                if (luaL_loadbufferx(state.get(), bytes.data(), bytes.size(), label.c_str(), "t") != LUA_OK) {
                    throw std::runtime_error(lua_tostring(state.get(), -1));
                }
                bytes.clear();
                // Keep source locations for actionable runtime tracebacks.
                if (lua_dump(state.get(), dump, &bytes, 0) != 0) { throw std::runtime_error("Bytecode compilation failed: " + name); }
                lua_pop(state.get(), 1);
            }
            entries.push_back({std::move(name), std::move(bytes)});
        }
        for (const auto& entry : entries) {
            if (entry.name.ends_with(".satimport")) {
                const auto source = std::string_view(entry.name).substr(0, entry.name.size() - std::string_view(".satimport").size());
                if (std::none_of(entries.begin(), entries.end(), [&](const sat::PakEntry& candidate) { return candidate.name == source; })) {
                    throw std::runtime_error("Import metadata source spelling does not match a packed asset: " + entry.name);
                }
            }
            if (sat::isMeshAssetPath(entry.name) || sat::isTextureAssetPath(entry.name)) {
                const auto metadata = entry.name + ".satimport";
                if (std::filesystem::exists(root / std::filesystem::path(std::u8string(metadata.begin(), metadata.end()))) &&
                    std::none_of(entries.begin(), entries.end(), [&](const sat::PakEntry& candidate) { return candidate.name == metadata; })) {
                    throw std::runtime_error("Import metadata spelling does not match its asset: " + metadata);
                }
            }
        }
        // Windows file lookup can accept a case mismatch that an archive lookup
        // cannot. Validate the exact packed spelling before writing any archive.
        for (const auto& entity : scene.entities()) {
            if (!entity.hasMesh) { continue; }
            for (const auto* path : {&entity.mesh.meshPath, &entity.mesh.texturePath}) {
                if (!path->empty() && std::none_of(entries.begin(), entries.end(),
                    [&](const sat::PakEntry& entry) { return entry.name == *path; })) {
                    throw std::runtime_error("Asset path does not exactly match a packaged file: " + *path);
                }
            }
        }
        std::string error;
        const std::filesystem::path base = argv[2];
        std::string combinedManifest;
        for (const std::string group : {"scripts", "textures", "audio", "assets"}) {
            if (!grouped && group != "scripts") { continue; }
            sat::ScopedPakKey key;
            std::ifstream keyFile(grouped ? keyPath / (group + ".bin") : keyPath, std::ios::binary);
            keyFile.read(reinterpret_cast<char*>(key.bytes.data()), static_cast<std::streamsize>(key.bytes.size()));
            if (!keyFile || keyFile.peek() != std::char_traits<char>::eof()) { throw std::runtime_error("Invalid archive group key"); }
            keyFile.close();
            std::vector<sat::PakEntry> selected;
            for (auto& entry : entries) {
                if (entry.name.empty()) { continue; }
                std::string category = "scripts";
                if (entry.name.starts_with("Assets/")) {
                    auto extension = std::filesystem::path(entry.name).extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                    const std::string imageTypes = ".png .jpg .jpeg .bmp .tga .dds .ktx .ktx2 .hdr .exr .webp";
                    const std::string audioTypes = ".wav .ogg .mp3 .flac .aac .opus .aiff";
                    const auto contains = [&](const std::string& types) { return !extension.empty() && (" " + types + " ").find(" " + extension + " ") != std::string::npos; };
                    category = contains(imageTypes) ? "textures" : contains(audioTypes) ? "audio" : "assets";
                }
                if (!grouped || category == group) { selected.push_back(std::move(entry)); }
            }
            auto output = base;
            if (group != "scripts") { auto name = base.stem(); name += "." + group; name += base.extension(); output = base.parent_path() / name; }
            if (!sat::PakArchive::write(output, selected, error, key.bytes)) { throw std::runtime_error(error); }
            auto manifest = output; manifest += ".parts";
            combinedManifest += read(manifest);
        }
        auto manifest = base; manifest += ".parts";
        std::ofstream list(manifest, std::ios::binary | std::ios::trunc);
        list << combinedManifest; list.close();
        if (!list) { throw std::runtime_error("Cannot write archive-set manifest"); }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
