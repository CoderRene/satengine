#include "HierarchyPanel.hpp"

#include "AssetData.hpp"
#include "AssetImportSettings.hpp"
#include "AssetLibrary.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "module/ContentDragDrop.hpp"
#include "module/HierarchyDragDrop.hpp"

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sat {
namespace {

constexpr char kTreeNode[] = "##entity";
constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

void pushEntityId(EntityId id) {
    char key[2 * sizeof(EntityId)];
    const auto result = std::to_chars(key, key + sizeof(key), id, 16);
    ImGui::PushID(key, result.ptr);
}

EntityId draggedEntity() {
    return hierarchyPayloadEntity(ImGui::GetDragDropPayload());
}

} // namespace

struct HierarchyPanel::Impl {
    struct Links {
        std::size_t firstChild = kNoIndex;
        std::size_t nextSibling = kNoIndex;
    };

    Scene* m_scene = nullptr;
    const Project* m_project = nullptr;
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

    enum class Action { None, Remove, Duplicate, AddChild, Group, Reparent, MoveBefore, MoveAfter, AddMesh };
    Action m_action = Action::None;
    EntityId m_target = kInvalidEntityId;
    EntityId m_destination = kInvalidEntityId;
    std::string m_droppedMeshPath;
    std::string m_meshDropError;
    std::string m_duplicateError;

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

    void queue(Action action, EntityId target, EntityId destination = kInvalidEntityId) {
        m_action = action;
        m_target = target;
        m_destination = destination;
    }

    void moveSelection() {
        if (m_action == Action::Reparent) {
            if (!canDrop(m_target, m_destination)) {
                return;
            }
            for (const EntityId id : selectionRoots(m_target)) {
                (void)m_scene->setParent(id, m_destination);
            }
        } else if (!m_scene->moveEntitiesBeside(selectionRoots(m_target), m_destination,
                       m_action == Action::MoveAfter)) {
            return;
        }

        if (!m_selected.contains(m_target)) {
            selectOnly(m_target);
        } else {
            m_primary = m_target;
            m_selectionActivated = true;
        }
        m_reveal = m_target;
    }

    void removeSelection() {
        const auto& entities = m_scene->entities();
        // The background menu can add roots after drawTree builds the links.
        m_links.resize(entities.size());
        std::vector<bool> scheduled(entities.size());
        std::vector<EntityId> removalOrder;
        removalOrder.reserve(m_selected.size());
        for (const EntityId id : m_selected) {
            const auto root = m_scene->entityIndex(id);
            if (!root || scheduled[*root]) {
                continue;
            }

            const auto subtreeStart = removalOrder.size();
            scheduled[*root] = true;
            removalOrder.push_back(id);
            for (std::size_t i = subtreeStart; i < removalOrder.size(); ++i) {
                const auto index = *m_scene->entityIndex(removalOrder[i]);
                for (std::size_t child = m_links[index].firstChild; child != kNoIndex;
                     child = m_links[child].nextSibling) {
                    if (!scheduled[child]) {
                        scheduled[child] = true;
                        removalOrder.push_back(entities[child].id());
                    }
                }
            }
            // Earlier subtrees already precede this one. Reverse the new entries
            // so every child is removed before its parent and removal never has to orphan it.
            std::reverse(removalOrder.begin() + subtreeStart, removalOrder.end());
        }

        for (const EntityId id : removalOrder) {
            (void)m_scene->destroyEntity(id);
        }
        cancelRename();
        selectOnly(kInvalidEntityId);
    }

    void duplicateSelection() {
        struct Copy {
            std::size_t source;
            std::size_t parentCopy = kNoIndex;
            EntityId id = kInvalidEntityId;
        };

        const auto& entities = m_scene->entities();
        std::vector<Copy> copies;
        for (const EntityId root : selectionRoots(m_target)) {
            if (const auto index = m_scene->entityIndex(root)) {
                copies.push_back({*index});
            }
        }
        std::sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) {
            return a.source < b.source;
        });
        for (std::size_t i = 0; i < copies.size(); ++i) {
            for (std::size_t child = m_links[copies[i].source].firstChild; child != kNoIndex;
                 child = m_links[child].nextSibling) {
                copies.push_back({child, i});
            }
        }
        if (copies.empty()) {
            return;
        }
        if (entities.size() >= kMaxSceneEntities || copies.size() > kMaxSceneEntities - entities.size()) {
            m_duplicateError = "Duplicating the selection would exceed the scene entity limit.";
            return;
        }
        m_duplicateError.clear();

        std::unordered_set<EntityId> selection;
        selection.reserve(m_selected.size());
        EntityId primary = kInvalidEntityId;
        for (auto& copy : copies) {
            const Entity& source = entities[copy.source];
            const EntityId sourceId = source.id();
            const bool isRoot = copy.parentCopy == kNoIndex;
            const EntityId parent = isRoot ? source.parentId() : copies[copy.parentCopy].id;
            copy.id = m_scene->duplicateEntity(sourceId, parent, isRoot ? source.name + " Copy" : std::string{});
            if (m_selected.contains(sourceId)) {
                selection.insert(copy.id);
            }
            if (sourceId == m_primary) {
                primary = copy.id;
            }
        }

        selectOnly(primary == kInvalidEntityId ? copies.front().id : primary);
        selection.insert(m_primary);
        m_selected.swap(selection);
        cancelRename();
        m_reveal = m_primary;
    }

    void applyQueuedAction() {
        // Apply structural edits after traversal to keep links and entity references valid while drawing.
        switch (m_action) {
        case Action::Remove:
            removeSelection();
            break;
        case Action::Duplicate:
            duplicateSelection();
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
            Entity& groupEntity = m_scene->createEntity("Group");
            groupEntity.createdAsGroup = true;
            const EntityId group = groupEntity.id();
            (void)m_scene->setParent(group, parent);
            for (const EntityId id : roots) {
                (void)m_scene->setParent(id, group);
            }
            m_reveal = roots.empty() ? group : roots.front();
            selectOnly(group);
            break;
        }
        case Action::Reparent:
        case Action::MoveBefore:
        case Action::MoveAfter:
            moveSelection();
            break;
        case Action::AddMesh:
            addDroppedMesh();
            break;
        case Action::None:
            break;
        }
        m_action = Action::None;
    }

    void addDroppedMesh() {
        m_meshDropError.clear();
        const std::string_view path = m_droppedMeshPath;
        const auto nameStart = path.rfind('/') + 1;
        const auto nameEnd = path.rfind('.');
        std::string name(path.substr(nameStart, nameEnd - nameStart));
        if (m_scene->entityCount() >= kMaxSceneEntities) {
            m_meshDropError = "The scene has reached its entity limit.";
            return;
        }

        AssetImportSettings settings;
        PreparedAssetImport imported;
        if (m_project && isSceneModelAssetPath(path)) {
            const auto reader = fileAssetReader(m_project->root());
            std::string metadata;
            const auto result = reader(m_droppedMeshPath + ".satimport", metadata, m_meshDropError);
            if (result == AssetReadResult::Error ||
                (result == AssetReadResult::Found && !parseImportSettings(metadata, settings, m_meshDropError))) {
                return;
            }
            if (settings.preserveNodeHierarchy) {
                settings.mergeGeometry = false;
                if (!decodeAssetImport(reader, path, &settings, imported, m_meshDropError)) { return; }
                if (imported.mesh.nodes.size() >= kMaxSceneEntities - m_scene->entityCount()) {
                    m_meshDropError = "This model hierarchy would exceed the scene entity limit.";
                    return;
                }
            }
        }

        auto& root = m_scene->createEntity(std::move(name));
        const EntityId rootId = root.id();
        (void)m_scene->setParent(rootId, m_destination);
        if (!settings.preserveNodeHierarchy) {
            root.hasMesh = true;
            root.mesh.meshPath = path;
        } else {
            std::unordered_map<std::uint32_t, EntityId> ids;
            ids.reserve(imported.mesh.nodes.size());
            for (const auto& source : imported.mesh.nodes) {
                auto& child = m_scene->createEntity(source.name);
                child.transform.position = {source.origin[0], source.origin[1], source.origin[2]};
                if (source.sectionCount != 0) {
                    child.hasMesh = true;
                    child.mesh.meshPath = path;
                    child.mesh.meshNode = source.id;
                }
                (void)m_scene->setParent(child.id(), source.parent == 0 ? rootId : ids.at(source.parent));
                ids.emplace(source.id, child.id());
            }
        }
        m_reveal = rootId;
        selectOnly(rootId);
    }

    void acceptEntityDrop(EntityId child, const Entity* row) {
        const EntityId rowId = row ? row->id() : kInvalidEntityId;
        if (row) {
            const ImVec2 top = ImGui::GetItemRectMin();
            const ImVec2 bottom = ImGui::GetItemRectMax();
            const float edge = (bottom.y - top.y) * 0.25f;
            const float mouseY = ImGui::GetMousePos().y;
            const bool before = mouseY < top.y + edge;
            if (before || mouseY >= bottom.y - edge) {
                if (child == rowId || (m_selected.contains(child) && m_selected.contains(rowId)) ||
                    !canDrop(child, row->parentId())) {
                    return;
                }
                const auto* payload = ImGui::AcceptDragDropPayload(kEntityPayload,
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                if (payload && payload->IsPreview()) {
                    const float y = before ? top.y : bottom.y;
                    ImGui::GetWindowDrawList()->AddLine({top.x, y}, {bottom.x, y},
                        ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
                }
                if (payload && payload->IsDelivery()) {
                    queue(before ? Action::MoveBefore : Action::MoveAfter, child, rowId);
                }
                return;
            }
        }
        if (canDrop(child, rowId) && ImGui::AcceptDragDropPayload(kEntityPayload)) {
            queue(Action::Reparent, child, rowId);
        }
    }

    void acceptDrop(const Entity* row) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        if (const EntityId child = draggedEntity(); child != kInvalidEntityId) {
            acceptEntityDrop(child, row);
        } else {
            const auto path = draggedContentPath(ImGui::GetDragDropPayload());
            if (isMeshAssetPath(path) && ImGui::AcceptDragDropPayload(kContentFilePayload)) {
                m_droppedMeshPath = path;
                queue(Action::AddMesh, kInvalidEntityId, row ? row->id() : kInvalidEntityId);
            }
        }
        ImGui::EndDragDropTarget();
    }

    void expandBranch(std::size_t root) {
        const auto& entities = m_scene->entities();
        auto* storage = ImGui::GetStateStorage();
        std::vector<std::size_t> branches{root};
        for (std::size_t i = 0; i < branches.size(); ++i) {
            const std::size_t index = branches[i];
            pushEntityId(entities[index].id());
            const ImGuiID treeId = ImGui::GetID(kTreeNode);
            if (storage->GetInt(treeId) == 0) {
                storage->SetInt(treeId, 1);
            }
            ImGui::PopID();

            for (std::size_t child = m_links[index].firstChild; child != kNoIndex;
                 child = m_links[child].nextSibling) {
                if (m_links[child].firstChild != kNoIndex) {
                    branches.push_back(child);
                }
            }
        }
    }

    bool drawEntity(std::size_t index, bool readOnly) {
        const Entity& entity = m_scene->entities()[index];
        const bool hasChildren = m_links[index].firstChild != kNoIndex;
        const EntityId id = entity.id();
        pushEntityId(id);
        bool open = false;
        if (!readOnly && m_renaming == id) {
            open = hasChildren && ImGui::GetStateStorage()->GetInt(ImGui::GetID(kTreeNode)) != 0;
            drawRename();
        } else {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth;
            if (!hasChildren) {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (m_selected.contains(id)) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            open = ImGui::TreeNodeEx(kTreeNode, flags, "%s", entity.name.c_str());
            const auto& io = ImGui::GetIO();
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            if (clicked && io.KeyAlt && hasChildren) {
                m_clicked = kInvalidEntityId;
                ImGui::PopID();
                expandBranch(index);
                return true;
            }
            if (!readOnly) {
                if (clicked && !ImGui::IsItemToggledOpen()) {
                    if (io.KeyShift || io.KeyCtrl) {
                        m_rowSelection = id;
                    } else if (m_selected.size() <= 1 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        selectOnly(id);
                        beginRename(id);
                    } else {
                        // Keep the Inspector selection while dragging another entity.
                        m_clicked = id;
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
                acceptDrop(&entity);

                if (ImGui::BeginPopupContextItem("EntityContext")) {
                    if (!m_selected.contains(id)) {
                        selectOnly(id);
                    }
                    if (ImGui::MenuItem("Rename", "F2")) {
                        beginRename(id);
                    }
                    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                        queue(Action::Duplicate, id);
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

    std::size_t buildLinks() {
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
        return index;
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
        std::size_t index = buildLinks();

        // Keep ImGui IDs independent of tree depth and walk without recursion.
        m_visible.clear();
        const EntityId renaming = m_renaming;
        bool drewRename = false;
        while (index != kNoIndex) {
            const auto& links = m_links[index];
            m_visible.push_back(entities[index].id());
            drewRename |= entities[index].id() == renaming;
            if (drawEntity(index, readOnly) && links.firstChild != kNoIndex) {
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

HierarchyPanel::HierarchyPanel(Scene& scene, const Project* project)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->m_scene = &scene;
    m_impl->m_project = project;
}

HierarchyPanel::~HierarchyPanel() = default;

bool HierarchyPanel::selectionActivated() const { return m_impl->m_selectionActivated; }

int HierarchyPanel::selectedIndex() const {
    const auto index = m_impl->m_scene->entityIndex(m_impl->m_primary);
    return index ? static_cast<int>(*index) : -1;
}

const std::unordered_set<EntityId>& HierarchyPanel::selectedIds() const { return m_impl->m_selected; }

void HierarchyPanel::restoreSelection(const std::unordered_set<EntityId>& ids, EntityId primary) {
    m_impl->selectOnly(primary);
    m_impl->m_selected = ids;
    std::erase_if(m_impl->m_selected, [&](EntityId id) { return !m_impl->m_scene->entityById(id); });
    if (!m_impl->m_selected.contains(primary)) {
        m_impl->m_primary = m_impl->m_selected.empty() ? kInvalidEntityId : *m_impl->m_selected.begin();
    }
    m_impl->m_anchor = m_impl->m_primary;
    m_impl->m_reveal = m_impl->m_primary;
    m_impl->m_selectionActivated = false;
    m_impl->cancelRename();
}

void HierarchyPanel::toggleSelected(EntityId id) {
    if (m_impl->m_scene->entityById(id)) { m_impl->selectRow(id, false, true); }
    m_impl->m_reveal = m_impl->m_primary;
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

void HierarchyPanel::deleteSelection() {
    if (!m_impl->m_selected.empty()) {
        m_impl->buildLinks();
        m_impl->removeSelection();
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
    if (!impl.m_meshDropError.empty()) {
        ImGui::TextWrapped("%s", impl.m_meshDropError.c_str());
    }
    if (!impl.m_duplicateError.empty()) {
        ImGui::TextWrapped("%s", impl.m_duplicateError.c_str());
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
        impl.acceptDrop(nullptr);
    } else if (isMeshAssetPath(draggedContentPath(ImGui::GetDragDropPayload()))) {
        const float height = std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y);
        ImGui::Selectable("Drop here to add mesh at root", false, ImGuiSelectableFlags_None, ImVec2(0, height));
        impl.acceptDrop(nullptr);
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
        } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D) && !impl.m_selected.empty()) {
            impl.queue(Impl::Action::Duplicate, impl.m_primary);
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
