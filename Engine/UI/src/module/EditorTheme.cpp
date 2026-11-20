#include "EditorTheme.hpp"

#include <imgui.h>

namespace sat {

namespace {

constexpr ImVec4 rgb(int red, int green, int blue, float alpha = 1.0f) {
    constexpr float kChannelScale = 1.0f / 255.0f;
    return ImVec4(red * kChannelScale, green * kChannelScale, blue * kChannelScale, alpha);
}

} // namespace

void applySatTheme() {
    ImGui::StyleColorsDark(); // start from a known base

    ImGuiStyle& style = ImGui::GetStyle();

    // Shape
    style.WindowRounding     = 12.0f;
    style.ChildRounding      = 10.0f;
    style.FrameRounding      = 7.0f;
    style.PopupRounding      = 10.0f;
    style.ScrollbarRounding  = 8.0f;
    style.GrabRounding       = 7.0f;
    style.TabRounding        = 8.0f;
    style.MenuItemRounding   = 7.0f;

    // Spacing
    style.WindowPadding      = ImVec2(10.0f, 10.0f);
    style.FramePadding       = ImVec2(7.0f, 5.0f);
    style.ItemSpacing        = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing   = ImVec2(5.0f, 4.0f);
    style.IndentSpacing      = 16.0f;
    style.ScrollbarSize      = 12.0f;
    style.GrabMinSize        = 10.0f;

    // Borders
    style.WindowBorderSize   = 1.0f;
    style.FrameBorderSize    = 1.0f;
    style.TabBorderSize      = 0.0f;
    style.TabBarOverlineSize = 2.0f;

    // Docking
    style.DockingSeparatorSize = 5.0f;

    ImVec4* c = style.Colors;
    constexpr ImVec4 kCanvas = rgb(9, 18, 32);
    constexpr ImVec4 kPanel = rgb(13, 26, 47);
    constexpr ImVec4 kField = rgb(31, 52, 75);
    constexpr ImVec4 kHover = rgb(43, 66, 97);
    constexpr ImVec4 kSelected = rgb(45, 72, 111);
    constexpr ImVec4 kOutline = rgb(64, 84, 110);
    constexpr ImVec4 kBlue = rgb(69, 128, 220);
    constexpr ImVec4 kLightBlue = rgb(112, 162, 239);
    constexpr ImVec4 kYellow = rgb(255, 224, 47);

    // Backgrounds
    c[ImGuiCol_WindowBg]                    = kPanel;
    c[ImGuiCol_ChildBg]                     = kPanel;
    c[ImGuiCol_PopupBg]                     = kPanel;
    c[ImGuiCol_MenuBarBg]                   = kPanel;
    c[ImGuiCol_TitleBg]                     = kPanel;
    c[ImGuiCol_TitleBgActive]               = kField;
    c[ImGuiCol_TitleBgCollapsed]            = kPanel;
    c[ImGuiCol_DockingEmptyBg]              = kCanvas;
    c[ImGuiCol_DockingPreview]              = rgb(69, 128, 220, 0.70f);
    c[ImGuiCol_ModalWindowDimBg]            = rgb(9, 18, 32, 0.70f);

    // Frames / inputs
    c[ImGuiCol_FrameBg]                     = kField;
    c[ImGuiCol_FrameBgHovered]              = kHover;
    c[ImGuiCol_FrameBgActive]               = kSelected;
    c[ImGuiCol_CheckMark]                   = kLightBlue;
    c[ImGuiCol_SliderGrab]                  = kBlue;
    c[ImGuiCol_SliderGrabActive]            = kLightBlue;
    c[ImGuiCol_ScrollbarBg]                 = kCanvas;
    c[ImGuiCol_ScrollbarGrab]               = kOutline;
    c[ImGuiCol_ScrollbarGrabHovered]        = kBlue;
    c[ImGuiCol_ScrollbarGrabActive]         = kLightBlue;
    c[ImGuiCol_Button]                      = kField;
    c[ImGuiCol_ButtonHovered]               = kHover;
    c[ImGuiCol_ButtonActive]                = kSelected;
    c[ImGuiCol_Header]                      = kSelected;
    c[ImGuiCol_HeaderHovered]               = kHover;
    c[ImGuiCol_HeaderActive]                = kSelected;
    c[ImGuiCol_Tab]                         = kField;
    c[ImGuiCol_TabHovered]                  = kHover;
    c[ImGuiCol_TabSelected]                 = kSelected;
    c[ImGuiCol_TabSelectedOverline]         = kYellow;
    c[ImGuiCol_TabDimmed]                   = kPanel;
    c[ImGuiCol_TabDimmedSelected]           = kField;
    c[ImGuiCol_TabDimmedSelectedOverline]   = kBlue;
    c[ImGuiCol_TableHeaderBg]               = kField;
    c[ImGuiCol_TableBorderStrong]           = kOutline;
    c[ImGuiCol_TableBorderLight]            = kHover;

    // Text / borders
    c[ImGuiCol_Text]                        = rgb(243, 245, 251);
    c[ImGuiCol_TextDisabled]                = rgb(142, 161, 185);
    c[ImGuiCol_TextLink]                    = kLightBlue;
    c[ImGuiCol_TextSelectedBg]              = rgb(69, 128, 220, 0.40f);
    c[ImGuiCol_Border]                      = kOutline;
    c[ImGuiCol_Separator]                   = kOutline;
    c[ImGuiCol_SeparatorHovered]            = kBlue;
    c[ImGuiCol_SeparatorActive]             = kLightBlue;
    c[ImGuiCol_ResizeGrip]                  = kOutline;
    c[ImGuiCol_ResizeGripHovered]           = kBlue;
    c[ImGuiCol_ResizeGripActive]            = kLightBlue;
    c[ImGuiCol_DragDropTarget]              = kYellow;
    c[ImGuiCol_NavCursor]                   = kYellow;
    c[ImGuiCol_UnsavedMarker]               = kYellow;
}

} // namespace sat
