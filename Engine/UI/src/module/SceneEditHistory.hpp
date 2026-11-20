#pragma once

#include "Scene.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sat {

// Window-thread history for the authored scene. The baseline is kept current
// incrementally so regular widget and gizmo editing only copies changed data.
class SceneEditHistory {
public:
    struct Selection {
        std::unordered_set<EntityId> ids;
        EntityId primary = kInvalidEntityId;
    };

    explicit SceneEditHistory(const Scene& scene);
    void reset(const Scene& scene);
    void markSaved();
    [[nodiscard]] bool dirty(const Scene& scene) const;
    [[nodiscard]] bool needsCapture(const Scene& scene) const;
    [[nodiscard]] const Scene& baseline() const { return m_baseline; }

    // Omit IDs for a structural edit. An empty span captures settings only.
    void capture(const Scene& scene, const char* label, std::uint64_t gesture,
        std::optional<std::span<const EntityId>> ids, const Selection* before = nullptr, const Selection* after = nullptr);
    // The Content Browser has already moved the file. Migrate both sides of
    // every scene edit so undoing an unrelated change keeps valid references.
    void relocateReferences(Scene& scene, const std::function<bool(Entity&)>& updateEntity,
        const std::function<bool(LightingSettings&)>& updateLighting);
    void finishGesture();
    [[nodiscard]] const char* undoLabel() const;
    [[nodiscard]] const char* redoLabel() const;
    [[nodiscard]] std::optional<Selection> step(Scene& scene, bool undo);

private:
    struct EntityChange {
        EntityId id;
        std::optional<Entity> before;
        std::optional<Entity> after;
    };
    struct Settings {
        PhysicsSettings physics;
        AudioSettings audio;
        LightingSettings lighting;

        explicit Settings(const Scene& scene);
        void apply(Scene& scene) const;
        bool operator==(const Settings&) const = default;
    };
    struct Entry {
        std::string label;
        std::unordered_map<EntityId, EntityChange> entities;
        std::vector<EntityId> beforeOrder;
        std::vector<EntityId> afterOrder;
        std::optional<Settings> beforeSettings;
        std::optional<Settings> afterSettings;
        std::optional<Selection> beforeSelection;
        std::optional<Selection> afterSelection;
        std::uint64_t beforeState = 0;
        std::uint64_t afterState = 0;

        [[nodiscard]] bool empty() const;
    };

    void apply(Scene& scene, const Entry& entry, bool undo) const;

    Scene m_baseline;
    std::deque<Entry> m_entries;
    std::optional<Entry> m_pending;
    std::size_t m_cursor = 0;
    std::uint64_t m_gesture = 0;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextState = 0;
    std::uint64_t m_current = 0;
    std::optional<std::uint64_t> m_saved = 0;
};

} // namespace sat
