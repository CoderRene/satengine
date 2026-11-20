# Generate the editor's ImGui draw unit with a six-dot dock grip.
set(imgui_draw_source "${imgui_SOURCE_DIR}/imgui_draw.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${imgui_draw_source}")
file(READ "${imgui_draw_source}" imgui_draw)

set(imgui_dock_arrow [=[
void ImGui::RenderArrowDockMenu(ImDrawList* draw_list, ImVec2 p_min, float sz, ImU32 col)
{
    draw_list->AddRectFilled(p_min + ImVec2(sz * 0.20f, sz * 0.15f), p_min + ImVec2(sz * 0.80f, sz * 0.30f), col);
    draw_list->AddTriangleFilled(p_min + ImVec2(sz * 0.20f, sz * 0.45f), p_min + ImVec2(sz * 0.80f, sz * 0.45f), p_min + ImVec2(sz * 0.50f, sz * 0.85f), col);
}
]=])

string(FIND "${imgui_draw}" "${imgui_dock_arrow}" imgui_dock_arrow_pos)
if(imgui_dock_arrow_pos EQUAL -1)
    message(FATAL_ERROR "The ImGui dock menu glyph changed. Update ImGuiDockGrip.cmake.")
endif()

string(REPLACE "${imgui_dock_arrow}" [=[
void ImGui::RenderArrowDockMenu(ImDrawList* draw_list, ImVec2 p_min, float sz, ImU32 col)
{
    const ImVec2 center = p_min + ImVec2(sz * 0.5f, sz * 0.5f);
    const float spacing = sz * 0.20f;
    const float radius = sz * 0.07f;
    for (int row = -1; row <= 1; ++row)
    {
        const float y = center.y + row * spacing;
        draw_list->AddCircleFilled(ImVec2(center.x - spacing * 0.5f, y), radius, col, 8);
        draw_list->AddCircleFilled(ImVec2(center.x + spacing * 0.5f, y), radius, col, 8);
    }
}
]=] imgui_draw "${imgui_draw}")

set(imgui_draw_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_draw.cpp")
file(GENERATE OUTPUT "${imgui_draw_generated}" CONTENT "${imgui_draw}")
