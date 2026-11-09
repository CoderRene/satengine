#include "Window.hpp"

#include "EnginePaths.hpp"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>
#include <memory>

namespace sat {

struct Window::Impl {
    GLFWwindow* window = nullptr;
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

static void setWindowIcon(GLFWwindow* window) {
    const char* paths[] = {
        SAT_ENGINE_ASSETS_DIR "/SatEngine_ic.png",
        "SatEngine_ic.png",
    };

    for (const char* path : paths) {
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load(path, &width, &height, &channels, 4);
        if (pixels == nullptr) {
            continue;
        }

        GLFWimage image{};
        image.width = width;
        image.height = height;
        image.pixels = pixels;
        glfwSetWindowIcon(window, 1, &image);
        stbi_image_free(pixels);
        return;
    }

    std::cerr << "Failed to load window icon: SatEngine_ic.png\n";
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

    setWindowIcon(m_impl->window);
#if defined(_WIN32)
    enableDarkTitleBar(m_impl->window);
#endif

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
    glfwPollEvents();
}

void* Window::nativeHandle() const {
    return m_impl->window;
}

} // namespace sat