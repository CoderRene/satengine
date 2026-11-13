#pragma once

#include "AssetLibrary.hpp"
#include "AudioSystem.hpp"
#include "LuaRuntime.hpp"
#include "PhysicsSystem.hpp"
#include "Scene.hpp"
#include "ScriptLog.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sat {

class Input;

// Window-thread play runtime. The borrowed Input must outlive this object.
// Authored scene edits are copied in; simulation changes stay here.
class PlaySession {
public:
    explicit PlaySession(const Input& input);
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    PlaySession(PlaySession&&) = delete;
    PlaySession& operator=(PlaySession&&) = delete;

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool decisionPending() const { return !m_active && m_prePlay.has_value(); }
    [[nodiscard]] bool takeFocusGame();
    [[nodiscard]] bool takeFocusLog();
    [[nodiscard]] const Scene& viewScene(const Scene& authored) const;
    [[nodiscard]] Scene& game() { return m_game; }
    [[nodiscard]] const Scene& game() const { return m_game; }
    [[nodiscard]] ScriptLog& log() { return m_log; }
    [[nodiscard]] std::size_t physicsWorldCount() const;

    void setAssetReader(AssetReader reader);
    [[nodiscard]] bool update(float deltaSeconds);
    void stop(const Scene& authored);
    void discardInto(Scene& authored);
    void acceptEdits();
    void defer(std::function<void()> action);
    [[nodiscard]] std::function<void()> takePendingAction();
    [[nodiscard]] bool start(Scene& authored, const std::filesystem::path& projectRoot);
    void applyEdits(Scene& authored, std::string& error);

private:
    Scene m_game;
    std::optional<Scene> m_applied;
    ScriptLog m_log;
    PhysicsSystem m_physics;
    AudioSystem m_audio;
    LuaRuntime m_scripts;
    std::optional<Scene> m_prePlay;
    std::function<void()> m_afterDecision;
    bool m_active = false;
    bool m_focusGame = false;
    bool m_focusLog = false;
};

} // namespace sat
