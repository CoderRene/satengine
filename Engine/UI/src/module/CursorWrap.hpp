#pragma once

#include <imgui.h>

#include <utility>

namespace sat {

// Relative value drags can continue across the editor window's edges.
void wrapActiveValueDrag();

template <typename... Args>
bool dragFloat(Args&&... args) {
    const bool changed = ImGui::DragFloat(std::forward<Args>(args)...);
    wrapActiveValueDrag();
    return changed;
}

template <typename... Args>
bool dragFloat3(Args&&... args) {
    const bool changed = ImGui::DragFloat3(std::forward<Args>(args)...);
    wrapActiveValueDrag();
    return changed;
}

template <typename... Args>
bool dragScalar(Args&&... args) {
    const bool changed = ImGui::DragScalar(std::forward<Args>(args)...);
    wrapActiveValueDrag();
    return changed;
}

} // namespace sat
