#include "EditorTheme.hpp"

#include <imgui.h>

namespace sat {

void applySatTheme() {
    ImGui::StyleColorsDark(); // start from a known base

    ImGuiStyle& style = ImGui::GetStyle();

    // Shape
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    // Spacing
    style.WindowPadding    = ImVec2(8.0f, 8.0f);
    style.FramePadding     = ImVec2(6.0f, 4.0f);
    style.ItemSpacing      = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.IndentSpacing    = 16.0f;
    style.ScrollbarSize    = 12.0f;
    style.GrabMinSize      = 10.0f;

    // Borders
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 0.0f;
    style.TabBorderSize    = 0.0f;

    // Docking
    style.DockingSeparatorSize = 2.0f;

    ImVec4* c = style.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.12f, 0.12f, 0.13f, 0.98f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);

    // Frames / inputs
    c[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);

    // Accent (pick one brand color and reuse it)
    const ImVec4 accent              = ImVec4(0.35f, 0.60f, 0.95f, 1.00f);
    const ImVec4 accentHover         = ImVec4(0.45f, 0.68f, 1.00f, 1.00f);
    const ImVec4 accentActive        = ImVec4(0.28f, 0.50f, 0.85f, 1.00f);

    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentActive;
    c[ImGuiCol_Button]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = accentHover;
    c[ImGuiCol_ButtonActive]         = accentActive;
    c[ImGuiCol_Header]               = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.35f, 0.48f, 1.00f);
    c[ImGuiCol_HeaderActive]         = accentActive;
    c[ImGuiCol_Tab]                  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TabHovered]           = accentHover;
    c[ImGuiCol_TabSelected]        = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_TabDimmed]          = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]  = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

    // Text / borders
    c[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.30f, 0.34f, 0.40f);
    c[ImGuiCol_ResizeGripHovered]    = accentHover;
    c[ImGuiCol_ResizeGripActive]     = accentActive;
}

} // namespace sat
