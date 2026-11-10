#include "LuaLogPanel.hpp"

#include "ScriptLog.hpp"

#include <imgui.h>
#include <memory>
#include <string>

namespace sat {

struct LuaLogPanel::Impl {
    ScriptLog* log = nullptr;
};

LuaLogPanel::LuaLogPanel(ScriptLog& log)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->log = &log;
}

LuaLogPanel::~LuaLogPanel() = default;

void LuaLogPanel::onImGuiRender(bool focus) {
    if (focus) {
        ImGui::SetNextWindowFocus();
    }
    ImGui::Begin("Lua Log");
    if (ImGui::Button("Clear")) {
        m_impl->log->clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy All")) {
        std::string all;
        for (const auto& entry : m_impl->log->entries()) {
            if (!all.empty()) {
                all.push_back('\n');
            }
            all += entry.message;
        }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::Separator();
    ImGui::BeginChild("LuaLogEntries", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (std::size_t index = 0; index < m_impl->log->entries().size(); ++index) {
        const auto& entry = m_impl->log->entries()[index];
        ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
        if (entry.level == LogLevel::Error) {
            color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        } else if (entry.level == LogLevel::Warn) {
            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        }
        ImGui::PushID(static_cast<int>(index));
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(entry.message.c_str());
        ImGui::PopStyleColor();
        if (ImGui::BeginPopupContextItem("entry")) {
            if (ImGui::MenuItem("Copy")) {
                ImGui::SetClipboardText(entry.message.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (focus || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sat