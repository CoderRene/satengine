#include "CursorWrap.hpp"

#include <imgui_internal.h>

namespace sat {

void wrapActiveValueDrag() {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    const ImGuiIO& io = context.IO;
    if (!ImGui::IsItemActive() || context.ActiveIdSource != ImGuiInputSource_Mouse ||
        ImGui::TempInputIsActive(context.ActiveId) || !ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
        io.AppFocusLost || !(io.BackendFlags & ImGuiBackendFlags_HasSetMousePos)) {
        return;
    }

    const ImGuiViewport& viewport = *ImGui::GetMainViewport();
    constexpr float kEdgeInset = 3.0f;
    if (viewport.Size.x <= 2.0f * kEdgeInset || !ImGui::IsMousePosValid()) {
        return;
    }
    const float left = viewport.Pos.x;
    const float right = left + viewport.Size.x;
    const ImVec2 mouse = io.MousePos;
    float wrappedX = mouse.x;
    if (mouse.x <= left + 1.0f && io.MouseDelta.x < 0.0f) {
        wrappedX = right - kEdgeInset;
    } else if (mouse.x >= right - 2.0f && io.MouseDelta.x > 0.0f) {
        wrappedX = left + kEdgeInset;
    }
    if (wrappedX != mouse.x) {
        // Discard the warp delta while retaining ImGui's active drag.
        ImGui::TeleportMousePos(ImVec2(wrappedX, mouse.y));
    }
}

} // namespace sat
