#include "HierarchyPanel.hpp"

#include "Scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace sat {

struct HierarchyPanel::Impl {
    Scene* scene = nullptr;
    bool selectionActivated = false;
    EntityId selected = kInvalidEntityId;
    int renaming = -1;
    char renameBuffer[128] = {};
    bool focusRename = false;
    bool renameWasActive = false;

    void beginRename(int index) {
        const auto& entities = scene->entities();
        if (index < 0 || index >= static_cast<int>(entities.size())) {
            return;
        }
        selected = entities[static_cast<std::size_t>(index)].id();
        renaming = index;
        const std::string& name = entities[static_cast<std::size_t>(index)].name;
        const std::size_t copyLen =
            (std::min)(name.size(), sizeof(renameBuffer) - 1);
        std::memcpy(renameBuffer, name.data(), copyLen);
        renameBuffer[copyLen] = '\0';
        focusRename = true;
        renameWasActive = false;
    }

    void commitRename() {
        if (renaming < 0) {
            return;
        }
        if (!scene->rename(static_cast<std::size_t>(renaming), renameBuffer)) {
            cancelRename();
            return;
        }
        renaming = -1;
        focusRename = false;
        renameWasActive = false;
    }

    void cancelRename() {
        renaming = -1;
        focusRename = false;
        renameWasActive = false;
    }

    void removeSelected() {
        if (selected == kInvalidEntityId || renaming >= 0) {
            return;
        }
        if (!scene->destroyEntity(selected)) {
            return;
        }
        cancelRename();
        selected = kInvalidEntityId;
    }
};

HierarchyPanel::HierarchyPanel(Scene& scene)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
}

HierarchyPanel::~HierarchyPanel() = default;

bool HierarchyPanel::selectionActivated() const { return m_impl->selectionActivated; }

int HierarchyPanel::selectedIndex() const {
    const auto index = m_impl->scene->entityIndex(m_impl->selected);
    return index ? static_cast<int>(*index) : -1;
}

void HierarchyPanel::setSelectedIndex(int index) {
    const int count = static_cast<int>(m_impl->scene->entities().size());
    if (index < -1 || index >= count) {
        m_impl->selected = kInvalidEntityId;
        return;
    }
    m_impl->selected = index >= 0 ? m_impl->scene->entity(static_cast<std::size_t>(index))->id() : kInvalidEntityId;
    if (m_impl->renaming >= 0 && m_impl->renaming != index) {
        m_impl->cancelRename();
    }
}

void HierarchyPanel::onImGuiRender(bool readOnly) {
    m_impl->selectionActivated = false;
    ImGui::Begin("Hierarchy");
    if (readOnly) {
        for (const auto& entity : m_impl->scene->entities()) {
            ImGui::TextUnformatted(entity.name.c_str());
        }
        ImGui::End();
        return;
    }
    const auto& entities = m_impl->scene->entities();
    for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
        ImGui::PushID(i);

        if (m_impl->renaming == i) {
            if (m_impl->focusRename) {
                ImGui::SetKeyboardFocusHere();
                m_impl->focusRename = false;
            }
            const ImGuiInputTextFlags flags =
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
            const bool enterPressed = ImGui::InputText(
                "##rename", m_impl->renameBuffer, sizeof(m_impl->renameBuffer), flags);
            if (ImGui::IsItemActive()) {
                m_impl->renameWasActive = true;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_impl->cancelRename();
                }
            } else if (enterPressed || (m_impl->renameWasActive && ImGui::IsItemDeactivated())) {
                m_impl->commitRename();
            }
        } else {
            if (ImGui::Selectable(
                    entities[static_cast<std::size_t>(i)].name.c_str(), m_impl->selected == entities[static_cast<std::size_t>(i)].id())) {
                m_impl->selectionActivated = true; m_impl->selected = entities[static_cast<std::size_t>(i)].id();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_impl->beginRename(i);
            }
            if (ImGui::BeginPopupContextItem("EntityContext")) {
                m_impl->selectionActivated = true; m_impl->selected = entities[static_cast<std::size_t>(i)].id();
                if (ImGui::MenuItem("Rename")) {
                    m_impl->beginRename(i);
                }
                if (ImGui::MenuItem("Remove", "Del")) {
                    m_impl->removeSelected();
                }
                ImGui::EndPopup();
            }
        }

        ImGui::PopID();
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered()) {
        m_impl->selectionActivated = true; m_impl->selected = kInvalidEntityId;
    }

    if (m_impl->selected != kInvalidEntityId && m_impl->renaming < 0 &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            m_impl->beginRename(selectedIndex());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            m_impl->removeSelected();
        }
    }

    if (ImGui::BeginPopupContextWindow(
            "HierarchyContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Add Empty Entity")) {
            m_impl->scene->addEmpty();
            m_impl->selectionActivated = true; m_impl->selected = m_impl->scene->entities().back().id();
        }
        if (ImGui::MenuItem("Add Camera")) {
            m_impl->scene->addCamera();
        }
        if (ImGui::MenuItem("Add Cube")) {
            m_impl->scene->addCube();
        }
        if (ImGui::MenuItem("Add Light")) {
            m_impl->scene->addLight();
            m_impl->selectionActivated = true;
            m_impl->selected = m_impl->scene->entities().back().id();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace sat
