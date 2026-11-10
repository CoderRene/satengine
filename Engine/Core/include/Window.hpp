#pragma once

#include "Input.hpp"

#include <memory>

namespace sat {

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const;
    void pollEvents();
    [[nodiscard]] const Input& input() const;
    void setIcon(const char* path);
    [[nodiscard]] void* nativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat