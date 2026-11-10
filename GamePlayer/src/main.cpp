#include "Application.hpp"
#include "EmbeddedLuaProject.hpp"
#include "LuaRuntime.hpp"
#include "SceneFile.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>
#include <system_error>

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
    LuaGame() : m_scripts(scene(), &input()) {}
    [[nodiscard]] bool failed() const { return m_failed; }
protected:
    void onStart() override {
        std::string error;
        if (!sat::deserializeScene(sat::embeddedLuaScene(), scene(), error)) {
            m_failed = true;
            std::cerr << "Cannot load embedded scene: " << error << '\n';
            requestStop();
            return;
        }
        if (!m_scripts.start(sat::embeddedLuaScripts())) {
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
