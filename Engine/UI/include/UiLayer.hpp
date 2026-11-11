#pragma once

#include <memory>

namespace sat {

class Renderer;
class Scene;
class Project;
class Window;

class UiLayer {
public:
    // All borrowed objects must outlive this layer. Use only on the window/render thread.
    UiLayer(Window& window, Renderer& renderer, Scene& scene, Project& project);
    ~UiLayer();

    UiLayer(const UiLayer&) = delete;
    UiLayer& operator=(const UiLayer&) = delete;

    [[nodiscard]] bool valid() const;
    [[nodiscard]] bool requestClose();
    void beginFrame();
    void endFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat