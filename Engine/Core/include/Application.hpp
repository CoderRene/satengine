#pragma once

#include "Scene.hpp"
#include "Input.hpp"

namespace sat {

// Runtime host. Subclass to initialize a scene and update game state.
// Hooks run on the window/render thread. deltaSeconds uses a steady clock.
class Application {
public:
    virtual ~Application() = default;
    [[nodiscard]] int run();

protected:
    [[nodiscard]] Scene& scene() { return m_scene; }
    [[nodiscard]] const Input& input() const { return m_input; }
    void requestStop() { m_stopRequested = true; }
    virtual void onStart() {}
    virtual void onUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void onStop() {}

private:
    Scene m_scene;
    Input m_input;
    bool m_stopRequested = false;
};

} // namespace sat
