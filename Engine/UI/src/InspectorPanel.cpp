#include "InspectorPanel.hpp"

#include "Scene.hpp"

#include <imgui.h>

#include <memory>

namespace sat {

struct InspectorPanel::Impl {
    Scene* scene = nullptr;
};

InspectorPanel::InspectorPanel(Scene& scene)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->scene = &scene;
}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::onImGuiRender(int selectedIndex) {
    ImGui::Begin("Inspector");

    Entity* entity = nullptr;
    if (selectedIndex >= 0) {
        entity = m_impl->scene->entity(static_cast<std::size_t>(selectedIndex));
    }

    if (entity == nullptr) {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(entity->name.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &entity->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &entity->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Scale", &entity->transform.scale.x, 0.05f);
    }

    if (entity->hasCamera &&
        ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Field of View", &entity->camera.fieldOfView, 0.1f, 1.0f, 179.0f);
        ImGui::DragFloat("Near Clip", &entity->camera.nearClip, 0.01f, 0.001f, 1000.0f);
        ImGui::DragFloat("Far Clip", &entity->camera.farClip, 1.0f, 0.1f, 100000.0f);
        ImGui::ColorEdit4("Clear Color", entity->camera.clearColor);
        bool isMain = entity->camera.isMain;
        if (ImGui::Checkbox("Main Camera", &isMain)) {
            if (isMain) {
                if (!m_impl->scene->setMainCamera(static_cast<std::size_t>(selectedIndex))) {
                    entity->camera.isMain = false;
                }
            } else {
                entity->camera.isMain = false;
            }
        }
    }

    if (entity->hasMesh && ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* primitiveName = "Cube";
        switch (entity->mesh.primitive) {
        case MeshComponent::Primitive::Cube:
            primitiveName = "Cube";
            break;
        }
        ImGui::Text("Primitive: %s", primitiveName);
    }

    ImGui::End();
}

} // namespace sat
