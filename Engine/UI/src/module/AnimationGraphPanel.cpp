#include "AnimationGraphPanel.hpp"
#include "AnimationAsset.hpp"
#include "AnimationSystem.hpp"
#include "AssetLibrary.hpp"
#include "AssetWrite.hpp"
#include "ContentDragDrop.hpp"
#include "Project.hpp"
#include "Renderer.hpp"
#include <imgui.h>
#include <imnodes.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace sat {
namespace {
bool textField(const char* label, std::string& value) {
    std::array<char, 4097> buffer{};
    std::copy_n(value.data(), std::min(value.size(), buffer.size() - 1), buffer.data());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) { return false; }
    value = buffer.data(); return true;
}
}
AnimationGraphPanel::AnimationGraphPanel(Project& project, Renderer& renderer) : m_project(project), m_renderer(renderer) {}
AnimationGraphPanel::~AnimationGraphPanel() { if (m_context) { ImNodes::DestroyContext(m_context); } }
void AnimationGraphPanel::load() {
    std::string bytes;
    if (fileAssetReader(m_project.root())(m_path, bytes, m_error) == AssetReadResult::Found && decodeAnimationGraph(bytes, m_graph, m_error)) {
        m_dirty = false; m_positions = true; m_state = m_transition = -1;
    }
}
void AnimationGraphPanel::open(std::string path) {
    if (m_dirty && path != m_path) { m_pendingPath = std::move(path); m_open = true; return; }
    m_path = std::move(path); load(); m_open = true;
}
void AnimationGraphPanel::draw(AnimationSystem* animation, EntityId entity) {
    if (!m_open) { return; }
    if (!m_context) { m_context = ImNodes::CreateContext(); }
    ImNodes::SetCurrentContext(m_context);
    ImGui::SetNextWindowSize(ImVec2(1000, 620), ImGuiCond_FirstUseEver);
    bool visible = true;
    if (!ImGui::Begin("Animation Graph", &visible)) { ImGui::End(); return; }
    if (!visible && !m_dirty) { m_open = false; }
    ImGui::Text("%s%s", m_path.c_str(), m_dirty ? " *" : "");
    if (ImGui::Button("Save")) {
        std::string bytes;
        if (encodeAnimationGraph(m_graph, bytes, m_error) && writeAsset(m_project.root() / std::filesystem::u8path(m_path), m_path, bytes, m_error)) {
            m_dirty = false; m_renderer.invalidateAsset(m_path);
        }
    }
    ImGui::SameLine(); if (ImGui::Button("Revert")) { load(); }
    if (!m_pendingPath.empty() || (!visible && m_dirty)) { ImGui::OpenPopup("Unsaved Animation Graph"); }
    if (ImGui::BeginPopupModal("Unsaved Animation Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Discard unsaved graph changes?");
        if (ImGui::Button("Discard")) {
            m_dirty = false;
            if (!m_pendingPath.empty()) { m_path = std::move(m_pendingPath); m_pendingPath.clear(); load(); }
            else { m_open = false; }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { m_pendingPath.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (!m_error.empty()) { ImGui::TextWrapped("%s", m_error.c_str()); }
    const auto debug = animation ? animation->debugView(entity) : AnimationDebugView{};
    if (ImGui::BeginTable("GraphLayout", 3, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 210);
        ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableNextColumn();
        if (ImGui::Button("Add Parameter") && m_graph.parameters.size() < 256) {
            m_graph.parameters.push_back({"Parameter " + std::to_string(m_graph.parameters.size() + 1)}); m_dirty = true;
        }
        for (std::size_t index = 0; index < m_graph.parameters.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            auto& parameter = m_graph.parameters[index];
            m_dirty |= textField("Name", parameter.name);
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo("Type", &type, "Bool\0Float\0Int\0Trigger\0")) { parameter.type = static_cast<AnimationParameterType>(type); parameter.value = 0; m_dirty = true; }
            m_dirty |= ImGui::InputDouble("Default", &parameter.value);
            if (animation && index < debug.parameters.size()) {
                double value = debug.parameters[index];
                if (ImGui::InputDouble("Live", &value)) {
                    switch (parameter.type) {
                    case AnimationParameterType::Bool: (void)animation->setBool(entity, parameter.name, value != 0); break;
                    case AnimationParameterType::Float: (void)animation->setFloat(entity, parameter.name, static_cast<float>(value)); break;
                    case AnimationParameterType::Int:
                        if (value >= INT32_MIN && value <= INT32_MAX) { (void)animation->setInt(entity, parameter.name, static_cast<std::int32_t>(value)); } break;
                    case AnimationParameterType::Trigger: if (value != 0) { (void)animation->setTrigger(entity, parameter.name); } break;
                    }
                }
            }
            if (ImGui::Button("Delete Parameter")) {
                for (auto& transition : m_graph.transitions) {
                    std::erase_if(transition.conditions, [&](const auto& condition) { return condition.parameter == index; });
                    for (auto& condition : transition.conditions) { if (condition.parameter > index) { --condition.parameter; } }
                }
                m_graph.parameters.erase(m_graph.parameters.begin() + index); m_dirty = true; ImGui::PopID(); break;
            }
            ImGui::Separator(); ImGui::PopID();
        }
        ImGui::TableNextColumn();
        ImNodes::BeginNodeEditor();
        ImNodes::BeginNode(0); ImNodes::BeginNodeTitleBar(); ImGui::TextUnformatted("Any State"); ImNodes::EndNodeTitleBar();
        ImNodes::BeginInputAttribute(0); ImGui::TextUnformatted("In"); ImNodes::EndInputAttribute();
        ImNodes::BeginOutputAttribute(1); ImGui::TextUnformatted("Out"); ImNodes::EndOutputAttribute(); ImNodes::EndNode();
        for (std::size_t index = 0; index < m_graph.states.size(); ++index) {
            auto& state = m_graph.states[index]; const int id = static_cast<int>(index + 1);
            if (m_positions) { ImNodes::SetNodeGridSpacePos(id, ImVec2(state.x, state.y)); }
            const bool active = state.name == debug.activeState;
            if (active) { ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 150, 90, 255)); }
            ImNodes::BeginNode(id); ImNodes::BeginNodeTitleBar();
            ImGui::Text("%s%s", state.name.c_str(), index == m_graph.defaultState ? " (Default)" : ""); ImNodes::EndNodeTitleBar();
            ImNodes::BeginInputAttribute(id * 2); ImGui::TextUnformatted("In"); ImNodes::EndInputAttribute();
            ImNodes::BeginOutputAttribute(id * 2 + 1); ImGui::TextUnformatted("Out"); ImNodes::EndOutputAttribute();
            if (active) { ImGui::ProgressBar(debug.transitionProgress, ImVec2(120, 0)); }
            ImNodes::EndNode(); if (active) { ImNodes::PopColorStyle(); }
        }
        for (std::size_t index = 0; index < m_graph.transitions.size(); ++index) {
            const auto& edge = m_graph.transitions[index];
            ImNodes::Link(static_cast<int>(index), (edge.source + 1) * 2 + 1, static_cast<int>((edge.target + 1) * 2));
        }
        ImNodes::EndNodeEditor(); m_positions = false;
        int output = 0, input = 0;
        if (ImNodes::IsLinkCreated(&output, &input)) {
            if (!(output & 1)) { std::swap(output, input); }
            if ((output & 1) && !(input & 1) && input >= 2) {
                AnimationTransition edge; edge.source = output / 2 - 1; edge.target = static_cast<std::uint32_t>(input / 2 - 1);
                m_graph.transitions.push_back(edge); m_dirty = true;
            }
        }
        int node = 0;
        if (ImNodes::NumSelectedNodes() == 1) { ImNodes::GetSelectedNodes(&node); m_state = node - 1; m_transition = -1; }
        if (ImNodes::NumSelectedLinks() == 1) { ImNodes::GetSelectedLinks(&m_transition); m_state = -1; }
        for (std::size_t index = 0; index < m_graph.states.size(); ++index) {
            const auto position = ImNodes::GetNodeGridSpacePos(static_cast<int>(index + 1)); auto& state = m_graph.states[index];
            if (state.x != position.x || state.y != position.y) { state.x = position.x; state.y = position.y; m_dirty = true; }
        }
        if (ImGui::BeginPopupContextItem("GraphContext")) {
            if (ImGui::MenuItem("Create State")) {
                m_graph.states.push_back({"State " + std::to_string(m_graph.states.size() + 1), "", 1, true, 100, 100}); m_positions = m_dirty = true;
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button("Add State")) { m_graph.states.push_back({"State " + std::to_string(m_graph.states.size() + 1), "", 1, true, 100, 100}); m_positions = m_dirty = true; }
        ImGui::TableNextColumn();
        if (m_state >= 0 && static_cast<std::size_t>(m_state) < m_graph.states.size()) {
            auto& state = m_graph.states[m_state];
            m_dirty |= textField("State", state.name);
            m_dirty |= textField("Clip", state.clip);
            if (ImGui::BeginDragDropTarget()) {
                const auto path = draggedContentPath(ImGui::GetDragDropPayload());
                if (isAnimationClipAssetPath(path) && ImGui::AcceptDragDropPayload(kContentFilePayload)) {
                    state.clip = path;
                    m_dirty = true;
                }
                ImGui::EndDragDropTarget();
            }
            textField("Preview Mesh", m_mesh);
            if (animation && !m_mesh.empty() && ImGui::BeginCombo("Embedded Clips", state.clip.c_str())) {
                for (const auto& name : animation->clipNames(m_mesh)) { if (ImGui::Selectable(name.c_str())) { state.clip = name; m_dirty = true; } }
                ImGui::EndCombo();
            }
            m_dirty |= ImGui::InputFloat("Speed", &state.speed); m_dirty |= ImGui::Checkbox("Loop", &state.loop);
            if (ImGui::Button("Set Default")) { m_graph.defaultState = static_cast<std::uint32_t>(m_state); m_dirty = true; }
            if (ImGui::Button("Delete State") && m_graph.states.size() > 1) {
                std::erase_if(m_graph.transitions, [&](const auto& edge) { return edge.source == m_state || edge.target == static_cast<unsigned>(m_state); });
                for (auto& edge : m_graph.transitions) { if (edge.source > m_state) { --edge.source; } if (edge.target > static_cast<unsigned>(m_state)) { --edge.target; } }
                if (m_graph.defaultState == static_cast<unsigned>(m_state)) { m_graph.defaultState = 0; }
                else if (m_graph.defaultState > static_cast<unsigned>(m_state)) { --m_graph.defaultState; }
                m_graph.states.erase(m_graph.states.begin() + m_state); m_state = -1; m_positions = m_dirty = true;
            }
        } else if (m_transition >= 0 && static_cast<std::size_t>(m_transition) < m_graph.transitions.size()) {
            auto& edge = m_graph.transitions[m_transition];
            m_dirty |= ImGui::InputFloat("Crossfade Seconds", &edge.durationSeconds, 0.01f);
            m_dirty |= ImGui::Checkbox("Exit Time", &edge.hasExitTime);
            if (edge.hasExitTime) { m_dirty |= ImGui::InputFloat("Normalized Time", &edge.exitTime); }
            m_dirty |= ImGui::Checkbox("Interrupt Self", &edge.canInterruptSelf);
            if (ImGui::Button("Add Condition") && !m_graph.parameters.empty()) { edge.conditions.push_back({}); m_dirty = true; }
            for (std::size_t index = 0; index < edge.conditions.size(); ++index) {
                ImGui::PushID(static_cast<int>(index)); auto& condition = edge.conditions[index];
                if (ImGui::BeginCombo("Parameter", m_graph.parameters[condition.parameter].name.c_str())) {
                    for (std::size_t p = 0; p < m_graph.parameters.size(); ++p) {
                        if (ImGui::Selectable(m_graph.parameters[p].name.c_str())) { condition.parameter = static_cast<std::uint32_t>(p); m_dirty = true; }
                    }
                    ImGui::EndCombo();
                }
                int operation = static_cast<int>(condition.operation);
                if (ImGui::Combo("Operator", &operation, "IsTrue\0IsFalse\0Greater\0Less\0Equals\0NotEquals\0Triggered\0")) { condition.operation = static_cast<AnimationConditionOperator>(operation); m_dirty = true; }
                m_dirty |= ImGui::InputDouble("Value", &condition.value);
                if (ImGui::Button("Remove Condition")) { edge.conditions.erase(edge.conditions.begin() + index); m_dirty = true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::Button("Delete Transition")) { m_graph.transitions.erase(m_graph.transitions.begin() + m_transition); m_transition = -1; m_dirty = true; }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
}
