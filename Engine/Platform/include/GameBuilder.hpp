#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace sat {

// Editor/window-thread only. Build runs in a separate process, with output in
// build.log. Destruction releases handles; an active build continues independently.
class GameBuilder {
public:
    enum class Status { Idle, Building, Succeeded, Failed };

    explicit GameBuilder(std::filesystem::path engineRoot);
    ~GameBuilder();
    GameBuilder(const GameBuilder&) = delete;
    GameBuilder& operator=(const GameBuilder&) = delete;
    GameBuilder(GameBuilder&&) = delete;
    GameBuilder& operator=(GameBuilder&&) = delete;

    // Exports to projectRoot/Build/<project-folder-name>.exe.
    [[nodiscard]] bool start(const std::filesystem::path& projectRoot);
    void poll();
    [[nodiscard]] Status status() const;
    [[nodiscard]] const std::string& message() const;
    // Fraction of four build stages completed, not a compilation/time estimate.
    [[nodiscard]] float progress() const;
    [[nodiscard]] double elapsedSeconds() const;
    [[nodiscard]] double secondsSinceOutput() const;
    // Bounded tail of build.log, refreshed incrementally while polling.
    [[nodiscard]] const std::string& logOutput() const;
    [[nodiscard]] const std::filesystem::path& outputFolder() const;
    [[nodiscard]] const std::filesystem::path& executablePath() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace sat
