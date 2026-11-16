#include "Window.hpp"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

#include <stb_image.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sat {

struct Window::Impl {
    GLFWwindow* window = nullptr;
    Input input;
    std::vector<FileDrop> fileDrops;
};

#if defined(_WIN32)
static void enableDarkTitleBar(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr) {
        return;
    }

    // 20 = Win10 20H1+; 19 = older builds that used the pre-rename value.
    constexpr DWORD kUseImmersiveDarkMode = 20;
    constexpr DWORD kUseImmersiveDarkModeBefore20H1 = 19;
    BOOL useDark = TRUE;
    if (FAILED(DwmSetWindowAttribute(
            hwnd, kUseImmersiveDarkMode, &useDark, sizeof(useDark)))) {
        DwmSetWindowAttribute(
            hwnd, kUseImmersiveDarkModeBefore20H1, &useDark, sizeof(useDark));
    }

    // Win11 caption/border colors — matches ImGui title/menu bar (~0.08, 0.08, 0.09).
    constexpr DWORD kCaptionColor = 35;
    constexpr DWORD kBorderColor = 255;
    const COLORREF titleColor = RGB(20, 20, 23);
    DwmSetWindowAttribute(hwnd, kCaptionColor, &titleColor, sizeof(titleColor));
    DwmSetWindowAttribute(hwnd, kBorderColor, &titleColor, sizeof(titleColor));
}
#endif

void Window::setIcon(const char* path) {
    if (m_impl->window == nullptr || path == nullptr) {
        return;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
    if (pixels == nullptr) {
        return;
    }
    GLFWimage image{width, height, pixels};
    glfwSetWindowIcon(m_impl->window, 1, &image);
    stbi_image_free(pixels);
}

Window::Window(int width, int height, const char* title)
    : m_impl(std::make_unique<Impl>()) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_impl->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (m_impl->window == nullptr) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return;
    }

#if defined(_WIN32)
    enableDarkTitleBar(m_impl->window);
#endif

    glfwSetWindowUserPointer(m_impl->window, m_impl.get());
    glfwSetKeyCallback(m_impl->window, [](GLFWwindow* window, int key, int, int action, int) {
        auto& input = static_cast<Impl*>(glfwGetWindowUserPointer(window))->input;
        if (action == GLFW_REPEAT) {
            input.repeatKey(static_cast<Key>(key));
        } else {
            input.setKey(static_cast<Key>(key), action == GLFW_PRESS);
        }
    });
    glfwSetMouseButtonCallback(m_impl->window, [](GLFWwindow* window, int button, int action, int) {
        static_cast<Impl*>(glfwGetWindowUserPointer(window))->input.setMouseButton(static_cast<MouseButton>(button), action == GLFW_PRESS);
    });
    glfwSetScrollCallback(m_impl->window, [](GLFWwindow* window, double x, double y) {
        static_cast<Impl*>(glfwGetWindowUserPointer(window))->input.addScroll(x, y);
    });
    glfwSetCharCallback(m_impl->window, [](GLFWwindow* window, unsigned int codepoint) {
        static_cast<Impl*>(glfwGetWindowUserPointer(window))->input.addText(static_cast<char32_t>(codepoint));
    });
    glfwSetCursorPosCallback(m_impl->window, [](GLFWwindow* window, double x, double y) {
        static_cast<Impl*>(glfwGetWindowUserPointer(window))->input.setCursorPosition(x, y);
    });
    glfwSetWindowFocusCallback(m_impl->window, [](GLFWwindow* window, int focused) {
        if (!focused) { static_cast<Impl*>(glfwGetWindowUserPointer(window))->input.releaseAll(); }
    });
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(m_impl->window, &x, &y);
    m_impl->input.setCursorPosition(x, y);

    glfwSetDropCallback(m_impl->window, [](GLFWwindow* window, int count, const char** paths) {
        auto& impl = *static_cast<Impl*>(glfwGetWindowUserPointer(window));
        try {
            // GLFW dispatches the drop's client coordinates to the cursor callback first.
            FileDrop drop{impl.input.cursorPosition(), {}};
            drop.paths.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) {
                if (paths[i] != nullptr) {
                    const std::string_view path(paths[i]);
                    drop.paths.emplace_back(std::u8string(path.begin(), path.end()));
                }
            }
            if (!drop.paths.empty()) {
                impl.fileDrops.push_back(std::move(drop));
            }
        } catch (const std::exception& error) {
            std::cerr << "Cannot receive dropped files: " << error.what() << '\n';
        }
    });

    glfwShowWindow(m_impl->window);
    glfwFocusWindow(m_impl->window);
}

Window::~Window() {
    if (m_impl->window != nullptr) {
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
    }
}

bool Window::shouldClose() const {
    return m_impl->window == nullptr || glfwWindowShouldClose(m_impl->window);
}

void Window::pollEvents() {
    m_impl->fileDrops.clear();
    m_impl->input.beginFrame();
    glfwPollEvents();
}

const Input& Window::input() const { return m_impl->input; }

std::span<const FileDrop> Window::fileDrops() const { return m_impl->fileDrops; }

void* Window::nativeHandle() const {
    return m_impl->window;
}

} // namespace sat