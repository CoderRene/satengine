#include "Input.hpp"

namespace sat {
void Input::beginFrame() {
    for (auto& state : m_keys) { state.pressed = state.released = false; }
    for (auto& state : m_mouse) { state.pressed = state.released = false; }
}
void Input::setButton(ButtonState& state, bool held) {
    if (state.held != held) {
        state.held = held;
        if (held) { state.pressed = true; } else { state.released = true; }
    }
}
void Input::setCursorPosition(double x, double y) { m_cursor = {x, y}; }
void Input::releaseAll() {
    for (auto& state : m_keys) { setButton(state, false); }
    for (auto& state : m_mouse) { setButton(state, false); }
}
void Input::setKey(Key value, bool held) {
    const auto index = static_cast<std::size_t>(value);
    if (index < m_keys.size()) { setButton(m_keys[index], held); }
}
bool Input::keyPressed(Key value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_keys.size() && m_keys[index].pressed;
}
bool Input::keyHeld(Key value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_keys.size() && m_keys[index].held;
}
bool Input::keyReleased(Key value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_keys.size() && m_keys[index].released;
}
void Input::setMouseButton(MouseButton value, bool held) {
    const auto index = static_cast<std::size_t>(value);
    if (index < m_mouse.size()) { setButton(m_mouse[index], held); }
}
bool Input::mousePressed(MouseButton value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_mouse.size() && m_mouse[index].pressed;
}
bool Input::mouseHeld(MouseButton value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_mouse.size() && m_mouse[index].held;
}
bool Input::mouseReleased(MouseButton value) const {
    const auto index = static_cast<std::size_t>(value);
    return index < m_mouse.size() && m_mouse[index].released;
}
} // namespace sat
