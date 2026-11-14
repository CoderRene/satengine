#include "Application.hpp"
#include "PakArchive.hpp"
#include "GamePakKeys.hpp"
#include "AudioSystem.hpp"
#include "LuaRuntime.hpp"
#include "PhysicsSystem.hpp"
#include "SceneFile.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::filesystem::path executableFolder() {
#if defined(_WIN32)
    std::wstring path(512, L'\0');
    while (path.size() <= 32768) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            break;
        }
        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }
        path.resize(path.size() * 2);
    }
#endif
    return {};
}

std::filesystem::path userLogFolder() {
#if defined(_WIN32)
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size > 0) {
        std::wstring value(size, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), size);
        if (length > 0 && length < size) {
            value.resize(length);
            return std::filesystem::path(value) / "SatEngine" / "Logs";
        }
    }
#endif
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::path{} : temporary / "SatEngine" / "Logs";
}

// Last-run error log. Both streams are restored before the file is destroyed.
class GameLog {
public:
    GameLog() {
        if (!open(executableFolder()) && !open(userLogFolder())) {
#if defined(_WIN32)
            OutputDebugStringW(L"SatGame could not create its error log.\n");
#endif
            return;
        }
        m_previousError = std::cerr.rdbuf(m_file.rdbuf());
        m_previousLog = std::clog.rdbuf(m_file.rdbuf());
    }
    ~GameLog() {
        if (m_previousError != nullptr) {
            std::cerr.rdbuf(m_previousError);
            std::clog.rdbuf(m_previousLog);
        }
    }
    GameLog(const GameLog&) = delete;
    GameLog& operator=(const GameLog&) = delete;
    GameLog(GameLog&&) = delete;
    GameLog& operator=(GameLog&&) = delete;
private:
    bool open(const std::filesystem::path& folder) {
        if (folder.empty()) {
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        if (error) {
            return false;
        }
        m_file.clear();
        m_file.open(folder / "SatGame.log", std::ios::out | std::ios::trunc);
        return m_file.is_open();
    }
    std::ofstream m_file;
    std::streambuf* m_previousError = nullptr;
    std::streambuf* m_previousLog = nullptr;
};

class LuaGame final : public sat::Application {
public:
    LuaGame() : m_physics(scene()), m_audio(scene()), m_scripts(scene(), &input(), nullptr, &m_physics, &m_audio) {}
    [[nodiscard]] bool failed() const { return m_failed; }
protected:
    void onStart() override {
        std::string error;
        const std::string_view pakName = SAT_GAME_PAK_NAME;
        const auto pakPath = executableFolder() / std::filesystem::path(std::u8string(pakName.begin(), pakName.end()));
        const auto loadGroup = [&](sat::PakArchive& archive, const std::filesystem::path& path, std::string_view group) {
            sat::ScopedPakKey key;
            if (!sat::loadGamePakKey(group, key.bytes)) { error = "Missing archive group key"; return false; }
            return archive.load(path, error, key.bytes);
        };
        if (!loadGroup(m_archive, pakPath, "scripts")) {
            m_failed = true;
            std::cerr << error << '\n';
            requestStop();
            return;
        }
        const std::array<std::string_view, 3> groups{"textures", "audio", "assets"};
        for (std::size_t index = 0; index < groups.size(); ++index) {
            auto name = pakPath.stem(); name += "." + std::string(groups[index]); name += pakPath.extension();
            if (!loadGroup(m_assets[index], pakPath.parent_path() / name, groups[index])) {
                m_failed = true;
                std::cerr << error << '\n'; requestStop(); return;
            }
        }
        if (!sat::deserializeScene(m_archive.find("Scene.sat"), scene(), error)) {
            m_failed = true;
            std::cerr << "Cannot load packed scene: " << error << '\n';
            requestStop();
            return;
        }
        const sat::AssetReader readAsset = [this](std::string_view path, std::string& bytes, std::string& assetError) {
            for (const auto& archive : m_assets) {
                const auto source = archive.find(path);
                if (!source.empty() || std::any_of(archive.entries().begin(), archive.entries().end(),
                    [&](const sat::PakEntry& entry) { return entry.name == path; })) {
                    if (source.size() > (path.ends_with(".satimport") ? 4096 : sat::kMaxAssetBytes)) {
                        assetError = "Packed asset exceeds read limit";
                        return sat::AssetReadResult::Error;
                    }
                    bytes.assign(source);
                    return sat::AssetReadResult::Found;
                }
            }
            assetError.clear();
            return sat::AssetReadResult::Missing;
        };
        setAssetReader(readAsset);
        setShaderReader([this](std::string_view path, std::string& bytes, std::string& shaderError) {
            if (!path.starts_with("Shaders/")) { return sat::AssetReadResult::Missing; }
            const auto source = m_assets[2].find(path);
            if (source.empty()) { return sat::AssetReadResult::Missing; }
            if (source.size() > 1024 * 1024) {
                shaderError = "Packed shader exceeds 1 MiB";
                return sat::AssetReadResult::Error;
            }
            bytes.assign(source);
            return sat::AssetReadResult::Found;
        });
        m_audio.setAssetReader(readAsset);
        std::vector<sat::LuaScriptSource> scripts;
        for (const auto& entry : m_archive.entries()) {
            if (entry.name.starts_with("Scripts/") && entry.name.ends_with(".lua")) {
                scripts.push_back({std::string_view(entry.name).substr(8), entry.bytes, true});
            }
        }
        m_scripts.setGameUi(gameUi());
        if (!m_scripts.start(scripts)) {
            reportFailure();
        }
    }
    void onUpdate(float deltaSeconds) override {
        if (m_scripts.running() && !m_scripts.update(deltaSeconds)) {
            reportFailure();
        }
    }
    void onStop() override {
        m_scripts.stop();
        m_scripts.setGameUi(nullptr);
        if (!m_failed && !m_scripts.error().empty()) {
            reportFailure();
        }
    }
private:
    void reportFailure() {
        m_failed = true;
        std::cerr << m_scripts.error() << '\n';
        requestStop();
    }
    sat::PakArchive m_archive;
    std::array<sat::PakArchive, 3> m_assets;
    sat::PhysicsSystem m_physics;
    sat::AudioSystem m_audio;
    sat::LuaRuntime m_scripts;
    bool m_failed = false;
};
} // namespace

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#else
int main() {
#endif
    GameLog log;
    try {
        LuaGame game;
        const int result = game.run();
        if (result != 0 && !game.failed()) {
            std::cerr << "Game initialization or rendering failed. Check graphics driver support.\n";
        }
        return game.failed() ? 1 : result;
    } catch (const std::exception& error) {
        std::cerr << "Unhandled game error: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Unhandled unknown game error.\n";
    }
    return 1;
}
