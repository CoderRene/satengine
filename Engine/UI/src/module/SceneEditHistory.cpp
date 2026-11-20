#include "SceneEditHistory.hpp"

#include <utility>

namespace sat {

SceneEditHistory::Settings::Settings(const Scene& scene)
    : physics(scene.physicsSettings), audio(scene.audioSettings), lighting(scene.lightingSettings) {}

void SceneEditHistory::Settings::apply(Scene& scene) const {
    scene.physicsSettings = physics;
    scene.audioSettings = audio;
    scene.lightingSettings = lighting;
}

bool SceneEditHistory::Entry::empty() const {
    return entities.empty() && beforeOrder == afterOrder && beforeSettings == afterSettings;
}

SceneEditHistory::SceneEditHistory(const Scene& scene) : m_baseline(scene), m_revision(scene.editRevision()) {}

void SceneEditHistory::reset(const Scene& scene) {
    *this = SceneEditHistory(scene);
}

void SceneEditHistory::markSaved() {
    finishGesture();
    m_saved = m_current;
}

bool SceneEditHistory::dirty(const Scene& scene) const {
    return needsCapture(scene) || m_current != m_saved || (m_pending && !m_pending->empty());
}

bool SceneEditHistory::needsCapture(const Scene& scene) const {
    return scene.editRevision() != m_revision;
}

void SceneEditHistory::capture(const Scene& scene, const char* label, std::uint64_t gesture,
    std::optional<std::span<const EntityId>> ids, const Selection* before, const Selection* after) {
    if (!needsCapture(scene)) { return; }
    if (m_pending && (gesture == 0 || gesture != m_gesture || m_pending->label != label)) {
        finishGesture();
    }
    if (!m_pending) {
        m_pending.emplace();
        m_pending->label = label;
        m_gesture = gesture;
    }
    auto& entry = *m_pending;
    if (before && !entry.beforeSelection) { entry.beforeSelection = *before; }
    if (after) { entry.afterSelection = *after; }

    const auto recordEntity = [&](EntityId id) {
        const auto* old = m_baseline.entityById(id);
        const auto* now = scene.entityById(id);
        if ((!old && !now) || (old && now && *old == *now)) { return; }
        auto [item, inserted] = entry.entities.try_emplace(id, EntityChange{id, {}, {}});
        if (inserted && old) { item->second.before = *old; }
        auto& change = item->second;
        if (now) { change.after = *now; }
        else { change.after.reset(); }
        if (change.before == change.after) { entry.entities.erase(item); }
    };
    if (ids) {
        for (EntityId id : *ids) {
            recordEntity(id);
            if (auto* old = m_baseline.entityById(id)) {
                const auto* now = scene.entityById(id);
                if (now && *old != *now) { *old = *now; }
            }
        }
    } else {
        const auto& oldEntities = m_baseline.entities();
        const auto& newEntities = scene.entities();
        bool orderChanged = oldEntities.size() != newEntities.size();
        for (std::size_t i = 0; i < oldEntities.size(); ++i) {
            recordEntity(oldEntities[i].id());
            orderChanged |= i < newEntities.size() && oldEntities[i].id() != newEntities[i].id();
        }
        for (const auto& item : newEntities) {
            if (!m_baseline.entityById(item.id())) { recordEntity(item.id()); }
        }
        if (orderChanged) {
            if (entry.beforeOrder.empty() && entry.afterOrder.empty()) {
                entry.beforeOrder.reserve(oldEntities.size());
                for (const auto& item : oldEntities) { entry.beforeOrder.push_back(item.id()); }
            }
            entry.afterOrder.clear();
            entry.afterOrder.reserve(newEntities.size());
            for (const auto& item : newEntities) { entry.afterOrder.push_back(item.id()); }
        }
    }
    const Settings old(m_baseline), now(scene);
    if (old != now) {
        if (!entry.beforeSettings) { entry.beforeSettings = old; }
        entry.afterSettings = now;
        if (ids) { now.apply(m_baseline); }
    }
    if (!ids) { m_baseline = scene; }
    m_revision = scene.editRevision();
    if (gesture == 0) { finishGesture(); }
}

void SceneEditHistory::relocateReferences(Scene& scene, const std::function<bool(Entity&)>& updateEntity,
    const std::function<bool(LightingSettings&)>& updateLighting) {
    finishGesture();
    bool changed = updateLighting(scene.lightingSettings);
    (void)updateLighting(m_baseline.lightingSettings);
    for (std::size_t i = 0; i < scene.entityCount(); ++i) {
        auto& entity = *scene.entity(i);
        changed |= updateEntity(entity);
        if (auto* baseline = m_baseline.entityById(entity.id())) { (void)updateEntity(*baseline); }
    }
    for (auto& entry : m_entries) {
        for (auto& [id, change] : entry.entities) {
            if (change.before) { changed |= updateEntity(*change.before); }
            if (change.after) { changed |= updateEntity(*change.after); }
        }
        if (entry.beforeSettings) { changed |= updateLighting(entry.beforeSettings->lighting); }
        if (entry.afterSettings) { changed |= updateLighting(entry.afterSettings->lighting); }
    }
    if (changed) {
        scene.markEdited();
        m_revision = scene.editRevision();
        m_saved.reset();
    }
}

void SceneEditHistory::finishGesture() {
    if (!m_pending) { return; }
    if (!m_pending->empty()) {
        while (m_entries.size() > m_cursor) { m_entries.pop_back(); }
        m_pending->beforeState = m_current;
        m_pending->afterState = ++m_nextState;
        m_current = m_nextState;
        m_entries.push_back(std::move(*m_pending));
        constexpr std::size_t kHistoryLimit = 128;
        if (m_entries.size() > kHistoryLimit) { m_entries.pop_front(); }
        m_cursor = m_entries.size();
    }
    m_pending.reset();
    m_gesture = 0;
}

const char* SceneEditHistory::undoLabel() const {
    return m_cursor == 0 ? nullptr : m_entries[m_cursor - 1].label.c_str();
}

const char* SceneEditHistory::redoLabel() const {
    return m_cursor == m_entries.size() ? nullptr : m_entries[m_cursor].label.c_str();
}

void SceneEditHistory::apply(Scene& scene, const Entry& entry, bool undo) const {
    if (entry.beforeOrder != entry.afterOrder) {
        const auto& order = undo ? entry.beforeOrder : entry.afterOrder;
        std::vector<Entity> entities;
        entities.reserve(order.size());
        for (EntityId id : order) {
            const auto change = entry.entities.find(id);
            if (change == entry.entities.end()) { entities.push_back(*scene.entityById(id)); }
            else { entities.push_back(*(undo ? change->second.before : change->second.after)); }
        }
        scene.restoreEditorEntities(std::move(entities));
    } else {
        for (const auto& [id, change] : entry.entities) {
            *scene.entityById(id) = *(undo ? change.before : change.after);
        }
        scene.markEdited();
    }
    const auto& settings = undo ? entry.beforeSettings : entry.afterSettings;
    if (settings) { settings->apply(scene); }
}

std::optional<SceneEditHistory::Selection> SceneEditHistory::step(Scene& scene, bool undo) {
    finishGesture();
    if (undo ? m_cursor == 0 : m_cursor == m_entries.size()) { return {}; }
    const auto& entry = m_entries[undo ? --m_cursor : m_cursor++];
    apply(scene, entry, undo);
    apply(m_baseline, entry, undo);
    m_revision = scene.editRevision();
    m_current = undo ? entry.beforeState : entry.afterState;
    return undo ? entry.beforeSelection : entry.afterSelection;
}

} // namespace sat
