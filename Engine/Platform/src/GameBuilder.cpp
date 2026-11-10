#include "GameBuilder.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sat {
namespace {
class NativeHandle {
public:
    explicit NativeHandle(HANDLE value = nullptr) : m_value(value) {}
    ~NativeHandle() { reset(); }
    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;
    NativeHandle(NativeHandle&&) = delete;
    NativeHandle& operator=(NativeHandle&&) = delete;
    [[nodiscard]] HANDLE get() const { return m_value; }
    [[nodiscard]] bool valid() const { return m_value != nullptr && m_value != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) {
        if (valid()) {
            CloseHandle(m_value);
        }
        m_value = value;
    }
private:
    HANDLE m_value;
};

std::wstring quoteArgument(const std::filesystem::path& path) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : path.native()) {
        if (character == L'\\') {
            ++slashes;
        } else {
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += character;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
std::string windowsError() {
    return std::system_category().message(static_cast<int>(GetLastError()));
}
} // namespace
} // namespace sat
#endif

namespace sat {
struct GameBuilder::Impl {
    explicit Impl(std::filesystem::path root) : m_engineRoot(std::move(root)) {}
    std::filesystem::path m_engineRoot;
    std::filesystem::path m_outputFolder;
    std::filesystem::path m_executablePath;
    Status m_status = Status::Idle;
    std::string m_message;
    float m_progress = 0.0f;
    std::string m_logOutput;
    std::string m_pendingLine;
    std::streamoff m_logOffset = 0;
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_startedAt{};
    Clock::time_point m_finishedAt{};
    Clock::time_point m_lastOutputAt{};
    Clock::time_point m_lastPollAt{};

    void readLog(bool finished = false) {
        constexpr std::size_t kMaxLogBytes = 128 * 1024;
        constexpr std::size_t kReadBytes = 64 * 1024;
        if (m_outputFolder.empty()) {
            return;
        }
        std::ifstream file(m_outputFolder / "build.log", std::ios::binary);
        if (!file) {
            return;
        }
        if (finished) {
            file.seekg(0, std::ios::end);
            const std::streamoff end = file.tellg();
            if (end > m_logOffset + static_cast<std::streamoff>(kReadBytes)) {
                m_logOffset = end - static_cast<std::streamoff>(kReadBytes);
                m_pendingLine.clear();
                m_logOutput.clear();
            }
        }
        file.seekg(m_logOffset);
        std::array<char, kReadBytes> bytes{};
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        const auto count = file.gcount();
        if (count <= 0) {
            return;
        }
        m_logOffset += count;
        m_lastOutputAt = Clock::now();
        m_logOutput.append(bytes.data(), static_cast<std::size_t>(count));
        if (m_logOutput.size() > kMaxLogBytes) {
            m_logOutput.erase(0, m_logOutput.size() - kMaxLogBytes);
            const auto newline = m_logOutput.find('\n');
            if (newline != std::string::npos) {
                m_logOutput.erase(0, newline + 1);
            }
        }
        m_pendingLine.append(bytes.data(), static_cast<std::size_t>(count));
        std::size_t consumed = 0;
        constexpr std::string_view kPrefix = "[SatBuild] ";
        while (true) {
            const auto newline = m_pendingLine.find('\n', consumed);
            if (newline == std::string::npos) {
                break;
            }
            std::string_view line(m_pendingLine.data() + consumed, newline - consumed);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (line.starts_with(kPrefix)) {
                line.remove_prefix(kPrefix.size());
                if (line.size() > 4 && line[0] >= '1' && line[0] <= '4' &&
                    line.substr(1, 3) == "/4 ") {
                    const float completed = static_cast<float>(line[0] - '1') / 4.0f;
                    if (completed >= m_progress) {
                        m_progress = completed;
                        m_message = std::string(line);
                    }
                }
            }
            consumed = newline + 1;
        }
        m_pendingLine.erase(0, consumed);
        if (m_pendingLine.size() > kReadBytes) {
            m_pendingLine.clear();
        }
    }
#if defined(_WIN32)
    NativeHandle m_process;
#endif
    bool fail(std::string message) {
        m_finishedAt = Clock::now();
        m_status = Status::Failed;
        m_message = std::move(message);
        return false;
    }
};

GameBuilder::GameBuilder(std::filesystem::path engineRoot)
    : m_impl(std::make_unique<Impl>(std::move(engineRoot))) {}
GameBuilder::~GameBuilder() = default;

bool GameBuilder::start(const std::filesystem::path& projectRoot) {
    auto& impl = *m_impl;
    if (impl.m_status == Status::Building) {
        return false;
    }
    impl.m_startedAt = Impl::Clock::now();
    impl.m_lastOutputAt = impl.m_startedAt;
    impl.m_lastPollAt = impl.m_startedAt;
    impl.m_progress = 0.0f;
    impl.m_logOffset = 0;
    impl.m_logOutput.clear();
    impl.m_pendingLine.clear();
    impl.m_outputFolder.clear();
    impl.m_executablePath.clear();
    if (projectRoot.empty()) {
        return impl.fail("Open a project before building.");
    }
#if defined(_WIN32)
    std::error_code error;
    auto project = std::filesystem::absolute(projectRoot, error).lexically_normal();
    if (project.filename().empty()) {
        project = project.parent_path();
    }
    if (error || !std::filesystem::is_regular_file(project / "SatEngineProject.sat", error) ||
        !std::filesystem::is_regular_file(project / "Scene.sat", error)) {
        return impl.fail("Project must contain SatEngineProject.sat and Scene.sat. Save the scene before building.");
    }
    const auto script = impl.m_engineRoot / "scripts/build.ps1";
    if (!std::filesystem::is_regular_file(script, error)) {
        return impl.fail("Cannot find the engine build script: " + script.string());
    }
    if (project.filename().empty()) {
        return impl.fail("The project must have a folder name for its executable.");
    }
    const auto output = project / "Build";
    std::filesystem::create_directories(output, error);
    if (error) {
        return impl.fail("Cannot create output folder: " + error.message());
    }
    impl.m_outputFolder = output;
    impl.m_executablePath = output / project.filename();
    impl.m_executablePath += L".exe";
    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return impl.fail("Cannot locate Windows PowerShell: " + windowsError());
    }
    const auto powershell = std::filesystem::path(systemDirectory) / "WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = quoteArgument(powershell) +
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quoteArgument(script) +
        L" -Target GamePlayer -Configuration Release -ProjectFolder " + quoteArgument(project);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    NativeHandle log(CreateFileW((output / "build.log").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!log.valid()) {
        return impl.fail("Cannot open build.log: " + windowsError());
    }
    NativeHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!input.valid()) {
        return impl.fail("Cannot initialize build process input: " + windowsError());
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = log.get();
    startup.hStdError = log.get();
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, impl.m_engineRoot.c_str(), &startup, &process)) {
        return impl.fail("Cannot start build: " + windowsError());
    }
    NativeHandle thread(process.hThread);
    impl.m_process.reset(process.hProcess);
    impl.m_status = Status::Building;
    impl.m_message = "1/4 Preparing build tools...";
    return true;
#else
    (void)projectRoot;
    return impl.fail("Standalone builds require Windows and Visual Studio with the C++ toolset.");
#endif
}

void GameBuilder::poll() {
#if defined(_WIN32)
    auto& impl = *m_impl;
    if (impl.m_status != Status::Building) {
        return;
    }
    const auto now = Impl::Clock::now();
    if (now - impl.m_lastPollAt >= std::chrono::milliseconds(200)) {
        impl.readLog();
        impl.m_lastPollAt = now;
    }
    const DWORD waitResult = WaitForSingleObject(impl.m_process.get(), 0);
    if (waitResult == WAIT_TIMEOUT) {
        return;
    }
    if (waitResult != WAIT_OBJECT_0) {
        impl.fail("Cannot poll build process: " + windowsError());
        impl.m_process.reset();
        return;
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(impl.m_process.get(), &exitCode)) {
        impl.fail("Cannot read build process status: " + windowsError());
        impl.m_process.reset();
        return;
    }
    impl.readLog(true);
    impl.m_finishedAt = Impl::Clock::now();
    impl.m_process.reset();
    std::error_code error;
    if (exitCode == 0 && std::filesystem::is_regular_file(impl.m_executablePath, error)) {
        impl.m_progress = 1.0f;
        impl.m_status = Status::Succeeded;
        impl.m_message = "Build succeeded. Run " + impl.m_executablePath.filename().string() + " from the project Build folder.";
    } else {
        impl.fail("Build failed (exit code " + std::to_string(exitCode) + "). See build.log for details.");
    }
#endif
}
GameBuilder::Status GameBuilder::status() const { return m_impl->m_status; }
const std::string& GameBuilder::message() const { return m_impl->m_message; }
float GameBuilder::progress() const { return m_impl->m_progress; }
double GameBuilder::elapsedSeconds() const {
    const auto& impl = *m_impl;
    if (impl.m_status == Status::Idle) {
        return 0.0;
    }
    const auto end = impl.m_status == Status::Building ? Impl::Clock::now() : impl.m_finishedAt;
    return std::chrono::duration<double>(end - impl.m_startedAt).count();
}
double GameBuilder::secondsSinceOutput() const {
    return std::chrono::duration<double>(Impl::Clock::now() - m_impl->m_lastOutputAt).count();
}
const std::string& GameBuilder::logOutput() const { return m_impl->m_logOutput; }
const std::filesystem::path& GameBuilder::outputFolder() const { return m_impl->m_outputFolder; }
const std::filesystem::path& GameBuilder::executablePath() const { return m_impl->m_executablePath; }
} // namespace sat
