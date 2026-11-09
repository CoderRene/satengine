#pragma once

#include <memory>

namespace sat {

class Renderer;
class Window;

class UiLayer {
public:
    UiLayer(Window& window, Renderer& renderer);
    ~UiLayer();

    UiLayer(const UiLayer&) = delete;
    UiLayer& operator=(const UiLayer&) = delete;

    void beginFrame();
    void endFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat