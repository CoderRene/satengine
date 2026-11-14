#pragma once

#include "Scene.hpp"
#include "Input.hpp"
#include "AssetLibrary.hpp"

#include <utility>

namespace sat {

class GameUi;

// Runtime host. Subclass to initialize a scene and update game state.
// Hooks run on the window/render thread. deltaSeconds uses a steady clock.
class Application {
public:
    virtual ~Application() = default;
    [[nodiscard]] int run();

protected:
    [[nodiscard]] Scene& scene() { return m_scene; }
    [[nodiscard]] const Input& input() const { return m_input; }
    // Borrowed during run(), including onStart() and onStop().
    [[nodiscard]] GameUi* gameUi() { return m_gameUi; }
    // Configure during onStart(). Captured sources must live through run().
    void setAssetReader(AssetReader reader) { m_assetReader = std::move(reader); }
    void setShaderReader(AssetReader reader) { m_shaderReader = std::move(reader); }
    void requestStop() { m_stopRequested = true; }
    virtual void onStart() {}
    virtual void onUpdate(float deltaSeconds) { (void)deltaSeconds; }
    virtual void onStop() {}

private:
    Scene m_scene;
    AssetReader m_assetReader;
    AssetReader m_shaderReader;
    Input m_input;
    GameUi* m_gameUi = nullptr;
    bool m_stopRequested = false;
};

} // namespace sat
