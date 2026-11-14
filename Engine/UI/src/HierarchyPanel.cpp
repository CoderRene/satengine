#include "HierarchyPanel.hpp"

#include "Scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace sat {
namespace {

constexpr char kEntityPayload[] = "SAT_HIERARCHY_ENTITY";
constexpr char kTreeNode[] = "##entity";
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

void pushEntityId(EntityId id) {
    char key[2 * sizeof(EntityId)];
    const auto result = std::to_chars(key, key + sizeof(key), id, 16);
    ImGui::PushID(key, result.ptr);
}

EntityId draggedEntity() {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    EntityId id = kInvalidEntityId;
    if (payload && payload->IsDataType(kEntityPayload) && payload->DataSize == sizeof(id)) {
        std::memcpy(&id, payload->Data, sizeof(id));
    }
    return id;
}

} // namespace

struct HierarchyPanel::Impl {
    struct Links {
        std::size_t firstChild = kNoIndex;
        std::size_t nextSibling = kNoIndex;
    };

    Scene* m_scene = nullptr;
    bool m_selectionActivated = false;
    std::uint64_t m_destructionRevision = 0;
    EntityId m_primary = kInvalidEntityId;
    EntityId m_anchor = kInvalidEntityId;
    EntityId m_clicked = kInvalidEntityId;
    EntityId m_rowSelection = kInvalidEntityId;
    std::unordered_set<EntityId> m_selected;
    std::vector<EntityId> m_visible;
    EntityId m_reveal = kInvalidEntityId;
    EntityId m_renaming = kInvalidEntityId;
    char m_renameBuffer[128] = {};
    bool m_focusRename = false;
    bool m_renameWasActive = false;
    std::vector<Links> m_links;
    std::vector<std::size_t> m_branchEnds;

    enum class Action { None, Remove, AddChild, Group, Reparent };
    Action m_action = Action::None;
    EntityId m_target = kInvalidEntityId;
    EntityId m_parent = kInvalidEntityId;

    void selectOnly(EntityId id) {
        m_clicked = kInvalidEntityId;
        m_selected.clear();
        if (id != kInvalidEntityId) {
            m_selected.insert(id);
        }
        m_primary = id;
        m_anchor = id;
        m_selectionActivated = true;
    }

    void selectRow(EntityId id, bool range, bool toggle) {
        if (range) {
            auto first = std::find(m_visible.begin(), m_visible.end(), m_anchor);
            auto last = std::find(m_visible.begin(), m_visible.end(), id);
            if (first != m_visible.end() && last != m_visible.end()) {
                if (first > last) {
                    std::swap(first, last);
                }
                if (!toggle) {
                    m_selected.clear();
                }
                m_selected.insert(first, last + 1);
                m_primary = id;
                m_selectionActivated = true;
                return;
            }
            if (toggle) {
                m_anchor = id;
                m_selected.insert(id);
                m_primary = id;
                m_selectionActivated = true;
                return;
            }
        } else if (toggle) {
            if (m_selected.erase(id) == 0) {
                m_selected.insert(id);
                m_primary = id;
            } else if (m_primary == id) {
                m_primary = kInvalidEntityId;
                for (auto row = m_visible.rbegin(); row != m_visible.rend(); ++row) {
                    if (m_selected.contains(*row)) {
                        m_primary = *row;
                        break;
                    }
                }
                if (m_primary == kInvalidEntityId && !m_selected.empty()) {
                    m_primary = *m_selected.begin();
                }
            }
            m_anchor = m_primary;
            m_selectionActivated = true;
            return;
        }
        selectOnly(id);
    }

    // Moving a selected ancestor also moves its descendants. Keep nested selections intact.
    bool isSelectionRoot(EntityId id) const {
        for (const Entity* item = m_scene->entityById(id); item && item->parentId() != kInvalidEntityId;) {
            if (m_selected.contains(item->parentId())) {
                return false;
            }
            item = m_scene->entityById(item->parentId());
        }
        return true;
    }

    bool canDrop(EntityId child, EntityId parent) const {
        if (child == kInvalidEntityId || !m_scene->canSetParent(child, parent)) {
            return false;
        }
        if (m_selected.size() > 1 && m_selected.contains(child)) {
            for (const EntityId id : m_selected) {
                if (isSelectionRoot(id) && !m_scene->canSetParent(id, parent)) {
                    return false;
                }
            }
        }
        return true;
    }

    std::vector<EntityId> selectionRoots(EntityId target) const {
        if (target == kInvalidEntityId) {
            return {};
        }
        if (m_selected.size() < 2 || !m_selected.contains(target)) {
            return {target};
        }
        std::vector<EntityId> roots;
        roots.reserve(m_selected.size());
        for (const EntityId id : m_selected) {
            if (isSelectionRoot(id)) {
                roots.push_back(id);
            }
        }
        return roots;
    }

    void beginRename(EntityId id) {
        const Entity* entity = m_scene->entityById(id);
        if (!entity) {
            return;
        }
        m_renaming = id;
        m_reveal = id;
        const std::size_t copyLen = (std::min)(entity->name.size(), sizeof(m_renameBuffer) - 1);
        std::memcpy(m_renameBuffer, entity->name.data(), copyLen);
        m_renameBuffer[copyLen] = '\0';
        m_focusRename = true;
        m_renameWasActive = false;
    }

    void cancelRename() {
        m_renaming = kInvalidEntityId;
        m_focusRename = false;
        m_renameWasActive = false;
    }

    void drawRename() {
        if (m_focusRename) {
            ImGui::SetKeyboardFocusHere();
            m_focusRename = false;
        }
        const ImGuiInputTextFlags flags =
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
        const bool enterPressed = ImGui::InputText("##rename", m_renameBuffer, sizeof(m_renameBuffer), flags);
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancelRename();
        } else if (enterPressed || (m_renameWasActive && ImGui::IsItemDeactivated())) {
            if (const auto index = m_scene->entityIndex(m_renaming)) {
                (void)m_scene->rename(*index, m_renameBuffer);
            }
            cancelRename();
        } else if (ImGui::IsItemActive()) {
            m_renameWasActive = true;
        }
    }

    void queue(Action action, EntityId target, EntityId parent = kInvalidEntityId) {
        m_action = action;
        m_target = target;
        m_parent = parent;
    }

    void applyQueuedAction() {
        // Apply structural edits after traversal, since removal can swap indices
        // and creation can invalidate references into the scene.
        switch (m_action) {
        case Action::Remove:
            for (const EntityId id : m_selected) {
                (void)m_scene->destroyEntity(id);
            }
            cancelRename();
            selectOnly(kInvalidEntityId);
            break;
        case Action::AddChild: {
            m_scene->addEmpty();
            const EntityId child = m_scene->entities().back().id();
            if (m_scene->setParent(child, m_target)) {
                m_reveal = child;
            }
            selectOnly(child);
            break;
        }
        case Action::Group: {
            const auto roots = selectionRoots(m_target);
            EntityId parent = roots.empty() ? kInvalidEntityId : m_scene->entityById(roots.front())->parentId();
            for (const EntityId id : roots) {
                if (m_scene->entityById(id)->parentId() != parent) {
                    parent = kInvalidEntityId;
                    break;
                }
            }
            const EntityId group = m_scene->createEntity("Group").id();
            (void)m_scene->setParent(group, parent);
            for (const EntityId id : roots) {
                (void)m_scene->setParent(id, group);
            }
            m_reveal = roots.empty() ? group : roots.front();
            selectOnly(group);
            break;
        }
        case Action::Reparent:
            if (canDrop(m_target, m_parent)) {
                for (const EntityId id : selectionRoots(m_target)) {
                    (void)m_scene->setParent(id, m_parent);
                }
                if (!m_selected.contains(m_target)) {
                    selectOnly(m_target);
                } else {
                    m_primary = m_target;
                    m_selectionActivated = true;
                }
                m_reveal = m_target;
            }
            break;
        case Action::None:
            break;
        }
        m_action = Action::None;
    }

    void acceptDrop(EntityId parent) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        const EntityId child = draggedEntity();
        if (canDrop(child, parent)) {
            if (ImGui::AcceptDragDropPayload(kEntityPayload)) {
                queue(Action::Reparent, child, parent);
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool drawEntity(const Entity& entity, bool hasChildren, bool readOnly) {
        const EntityId id = entity.id();
        pushEntityId(id);
        bool open = false;
        if (!readOnly && m_renaming == id) {
            open = hasChildren && ImGui::GetStateStorage()->GetInt(ImGui::GetID(kTreeNode), 1) != 0;
            drawRename();
        } else {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (!hasChildren) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (m_selected.contains(id)) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            open = ImGui::TreeNodeEx(kTreeNode, flags, "%s", entity.name.c_str());
            if (!readOnly) {
                const auto& io = ImGui::GetIO();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    if (io.KeyShift || io.KeyCtrl) {
                        m_rowSelection = id;
                    } else if (m_selected.size() <= 1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        selectOnly(id);
                        beginRename(id);
                    } else if (m_selected.contains(id)) {
                        // Keep the full selection until this click becomes a drag or is released.
                        m_clicked = id;
                    } else {
                        selectOnly(id);
                    }
                }
                if (m_clicked == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
                    selectOnly(id);
                }
                if (ImGui::BeginDragDropSource()) {
                    m_clicked = kInvalidEntityId;
                    ImGui::SetDragDropPayload(kEntityPayload, &id, sizeof(id));
                    if (m_selected.size() > 1 && m_selected.contains(id)) {
                        ImGui::Text("%zu selected", m_selected.size());
                    } else {
                        ImGui::TextUnformatted(entity.name.c_str());
                    }
                    ImGui::EndDragDropSource();
                }
                acceptDrop(id);

                if (ImGui::BeginPopupContextItem("EntityContext")) {
                    if (!m_selected.contains(id)) {
                        selectOnly(id);
                    }
                    if (ImGui::MenuItem("Rename", "F2")) {
                        beginRename(id);
                    }
                    if (ImGui::MenuItem("Add Empty Child")) {
                        queue(Action::AddChild, id);
                    }
                    if (ImGui::MenuItem("Group Selection")) {
                        queue(Action::Group, id);
                    }
                    if (ImGui::MenuItem("Move to Root", nullptr, false, (m_selected.size() > 1 || entity.parentId() != kInvalidEntityId))) {
                        queue(Action::Reparent, id);
                    }
                    if (ImGui::MenuItem("Remove", "Del")) {
                        queue(Action::Remove, id);
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::PopID();
        return open;
    }

    void drawTree(bool readOnly) {
        for (const Entity* item = m_scene->entityById(m_reveal); item;) {
            item = m_scene->entityById(item->parentId());
            if (item) {
                pushEntityId(item->id());
                ImGui::GetStateStorage()->SetInt(ImGui::GetID(kTreeNode), 1);
                ImGui::PopID();
            }
        }
        m_reveal = kInvalidEntityId;

        const auto& entities = m_scene->entities();
        m_links.assign(entities.size(), {});
        std::size_t index = kNoIndex;
        for (std::size_t i = entities.size(); i > 0; --i) {
            const std::size_t child = i - 1;
            const auto parent = m_scene->entityIndex(entities[child].parentId());
            std::size_t& head = parent ? m_links[*parent].firstChild : index;
            m_links[child].nextSibling = head;
            head = child;
        }

        // Keep ImGui IDs independent of tree depth and walk without recursion.
        m_visible.clear();
        const EntityId renaming = m_renaming;
        bool drewRename = false;
        while (index != kNoIndex) {
            const auto& links = m_links[index];
            m_visible.push_back(entities[index].id());
            drewRename |= entities[index].id() == renaming;
            if (drawEntity(entities[index], links.firstChild != kNoIndex, readOnly) && links.firstChild != kNoIndex) {
                m_branchEnds.push_back(links.nextSibling);
                ImGui::Indent();
                index = links.firstChild;
            } else {
                index = links.nextSibling;
            }
            while (index == kNoIndex && !m_branchEnds.empty()) {
                index = m_branchEnds.back();
                m_branchEnds.pop_back();
                ImGui::Unindent();
            }
        }
        if (renaming != kInvalidEntityId && !drewRename && m_renaming == renaming) {
            cancelRename();
        }
    }
};

HierarchyPanel::HierarchyPanel(Scene& scene)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->m_scene = &scene;
}

HierarchyPanel::~HierarchyPanel() = default;

bool HierarchyPanel::selectionActivated() const { return m_impl->m_selectionActivated; }

int HierarchyPanel::selectedIndex() const {
    const auto index = m_impl->m_scene->entityIndex(m_impl->m_primary);
    return index ? static_cast<int>(*index) : -1;
}

void HierarchyPanel::setSelectedIndex(int index) {
    const Entity* entity = index >= 0 ? m_impl->m_scene->entity(static_cast<std::size_t>(index)) : nullptr;
    const EntityId id = entity ? entity->id() : kInvalidEntityId;
    m_impl->selectOnly(id);
    m_impl->m_selectionActivated = false;
    m_impl->m_reveal = id;
    if (m_impl->m_renaming != id) {
        m_impl->cancelRename();
    }
}

void HierarchyPanel::onImGuiRender(bool readOnly) {
    auto& impl = *m_impl;
    impl.m_selectionActivated = false;
    if (impl.m_destructionRevision != impl.m_scene->destructionRevision()) {
        impl.m_destructionRevision = impl.m_scene->destructionRevision();
        std::erase_if(impl.m_selected, [&](EntityId id) { return !impl.m_scene->entityById(id); });
        if (!impl.m_selected.contains(impl.m_primary)) {
            impl.m_primary = impl.m_selected.empty() ? kInvalidEntityId : *impl.m_selected.begin();
            impl.m_anchor = impl.m_primary;
        }
    }
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }
    if (readOnly || !impl.m_scene->entityById(impl.m_renaming)) {
        impl.cancelRename();
    }
    const bool wasRenaming = impl.m_renaming != kInvalidEntityId;
    impl.m_rowSelection = kInvalidEntityId;
    impl.drawTree(readOnly);
    if (readOnly) {
        ImGui::End();
        return;
    }

    const auto& io = ImGui::GetIO();
    if (impl.m_rowSelection != kInvalidEntityId) {
        impl.selectRow(impl.m_rowSelection, io.KeyShift, io.KeyCtrl);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        impl.m_clicked = kInvalidEntityId;
    }

    if (draggedEntity() != kInvalidEntityId) {
        ImGui::Selectable("Drop here to move to root");
        impl.acceptDrop(kInvalidEntityId);
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered()) {
        impl.selectOnly(kInvalidEntityId);
    }

    if (!wasRenaming && impl.m_renaming == kInvalidEntityId &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
            if (!impl.m_visible.empty()) {
                impl.selectOnly(impl.m_visible.front());
                impl.m_selected.insert(impl.m_visible.begin(), impl.m_visible.end());
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !impl.m_selected.empty()) {
            impl.selectOnly(kInvalidEntityId);
        } else if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            impl.beginRename(impl.m_primary);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !impl.m_selected.empty()) {
            impl.queue(Impl::Action::Remove, impl.m_primary);
        }
    }

    if (ImGui::BeginPopupContextWindow(
            "HierarchyContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add Group")) {
            impl.queue(Impl::Action::Group, kInvalidEntityId);
        }
        if (ImGui::MenuItem("Add Empty Entity")) {
            impl.m_scene->addEmpty();
            impl.selectOnly(impl.m_scene->entities().back().id());
        }
        if (ImGui::MenuItem("Add Camera")) {
            impl.m_scene->addCamera();
        }
        if (ImGui::MenuItem("Add Cube")) {
            impl.m_scene->addCube();
        }
        if (ImGui::MenuItem("Add Light")) {
            impl.m_scene->addLight();
            impl.selectOnly(impl.m_scene->entities().back().id());
        }
        ImGui::EndPopup();
    }

    impl.applyQueuedAction();
    ImGui::End();
}

} // namespace sat
