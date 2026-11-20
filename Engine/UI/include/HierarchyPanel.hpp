#pragma once

#include "Scene.hpp"

#include <memory>
#include <unordered_set>

namespace sat {

class Project;
class Scene;

class HierarchyPanel {
public:
    explicit HierarchyPanel(Scene& scene, const Project* project = nullptr);
    ~HierarchyPanel();

    HierarchyPanel(const HierarchyPanel&) = delete;
    HierarchyPanel& operator=(const HierarchyPanel&) = delete;

    void onImGuiRender(bool readOnly = false);
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] bool selectionActivated() const;
    [[nodiscard]] const std::unordered_set<EntityId>& selectedIds() const;
    void restoreSelection(const std::unordered_set<EntityId>& ids, EntityId primary);
    void toggleSelected(EntityId id);
    void setSelectedIndex(int index);
    void deleteSelection();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
