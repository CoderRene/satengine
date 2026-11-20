#pragma once

#include "Input.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace sat {

struct FileDrop {
    CursorPosition position;
    std::vector<std::filesystem::path> paths;
};

class Window {
public:
    enum class FrameStyle { Dark, Editor };

    Window(int width, int height, const char* title, FrameStyle frameStyle = FrameStyle::Dark);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const;
    void pollEvents();
    [[nodiscard]] const Input& input() const;
    // Window-thread only. The client-coordinate drops are borrowed until the next pollEvents().
    [[nodiscard]] std::span<const FileDrop> fileDrops() const;
    void setIcon(const char* path);
    [[nodiscard]] void* nativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat