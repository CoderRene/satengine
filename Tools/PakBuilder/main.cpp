#include "PakArchive.hpp"
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
