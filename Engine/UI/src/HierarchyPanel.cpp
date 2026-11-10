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
    int selected = -1;
    int renaming = -1;
    char renameBuffer[128] = {};
    bool focusRename = false;
    bool renameWasActive = false;

    void beginRename(int index) {
        const auto& entities = scene->entities();
        if (index < 0 || index >= static_cast<int>(entities.size())) {
            return;
        }
        selected = index;
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
        if (selected < 0 || renaming >= 0) {
            return;
        }
        const int removeIndex = selected;
        if (!scene->remove(static_cast<std::size_t>(removeIndex))) {
            return;
        }
        cancelRename();
        selected = -1;
    }
};

HierarchyPanel::HierarchyPanel(Scene& scene)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
}

HierarchyPanel::~HierarchyPanel() = default;

int HierarchyPanel::selectedIndex() const {
    return m_impl->selected;
}

void HierarchyPanel::setSelectedIndex(int index) {
    const int count = static_cast<int>(m_impl->scene->entities().size());
    if (index < -1 || index >= count) {
        m_impl->selected = -1;
        return;
    }
    m_impl->selected = index;
    if (m_impl->renaming >= 0 && m_impl->renaming != index) {
        m_impl->cancelRename();
    }
}

void HierarchyPanel::onImGuiRender(bool readOnly) {
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
                    entities[static_cast<std::size_t>(i)].name.c_str(), m_impl->selected == i)) {
                m_impl->selected = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                m_impl->beginRename(i);
            }
            if (ImGui::BeginPopupContextItem("EntityContext")) {
                m_impl->selected = i;
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
        m_impl->selected = -1;
    }

    if (m_impl->selected >= 0 && m_impl->renaming < 0 &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2)) {
            m_impl->beginRename(m_impl->selected);
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
            m_impl->selected = static_cast<int>(m_impl->scene->entities().size() - 1);
        }
        if (ImGui::MenuItem("Add Camera")) {
            m_impl->scene->addCamera();
        }
        if (ImGui::MenuItem("Add Cube")) {
            m_impl->scene->addCube();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace sat
