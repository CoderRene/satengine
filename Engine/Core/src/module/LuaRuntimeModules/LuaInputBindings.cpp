#include "LuaInputBindings.hpp"

#include "Input.hpp"

#include <lua.hpp>

#include <cstddef>

namespace sat::lua {
namespace {

const Input& inputFor(lua_State* state) {
    static const Input kIdleInput;
    const auto* input = static_cast<const Input*>(lua_touserdata(state, lua_upvalueindex(1)));
    return input != nullptr ? *input : kIdleInput;
}

int keyPressed(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).keyPressed(static_cast<Key>(value)));
    return 1;
}

int keyHeld(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).keyHeld(static_cast<Key>(value)));
    return 1;
}

int keyReleased(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 348) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).keyReleased(static_cast<Key>(value)));
    return 1;
}

int mousePressed(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).mousePressed(static_cast<MouseButton>(value)));
    return 1;
}

int mouseHeld(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).mouseHeld(static_cast<MouseButton>(value)));
    return 1;
}

int mouseReleased(lua_State* state) {
    const lua_Integer value = luaL_checkinteger(state, 1);
    if (value < 0 || value > 7) {
        return luaL_argerror(state, 1, "invalid input code");
    }
    lua_pushboolean(state, inputFor(state).mouseReleased(static_cast<MouseButton>(value)));
    return 1;
}

int cursorPosition(lua_State* state) {
    const auto position = inputFor(state).cursorPosition();
    lua_pushnumber(state, position.x);
    lua_pushnumber(state, position.y);
    return 2;
}

struct NamedCode {
    const char* name;
    int code;
};

void pushNamedCodes(lua_State* state, const NamedCode* codes, std::size_t count) {
    lua_createtable(state, 0, static_cast<int>(count));
    for (std::size_t index = 0; index < count; ++index) {
        lua_pushinteger(state, codes[index].code);
        lua_setfield(state, -2, codes[index].name);
    }
}

} // namespace

void registerInputBindings(lua_State* state, const Input* input) {
    lua_newtable(state);
    const luaL_Reg inputBindings[] = {
        {"keyPressed", keyPressed},
        {"keyHeld", keyHeld},
        {"keyReleased", keyReleased},
        {"mousePressed", mousePressed},
        {"mouseHeld", mouseHeld},
        {"mouseReleased", mouseReleased},
        {"cursorPosition", cursorPosition},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, const_cast<Input*>(input));
    luaL_setfuncs(state, inputBindings, 1);
    lua_setfield(state, -2, "input");

    constexpr NamedCode kKeys[] = {
        {"Space", static_cast<int>(Key::Space)},
        {"Apostrophe", static_cast<int>(Key::Apostrophe)},
        {"Comma", static_cast<int>(Key::Comma)},
        {"Minus", static_cast<int>(Key::Minus)},
        {"Period", static_cast<int>(Key::Period)},
        {"Slash", static_cast<int>(Key::Slash)},
        {"Digit0", static_cast<int>(Key::Digit0)},
        {"Digit1", static_cast<int>(Key::Digit1)},
        {"Digit2", static_cast<int>(Key::Digit2)},
        {"Digit3", static_cast<int>(Key::Digit3)},
        {"Digit4", static_cast<int>(Key::Digit4)},
        {"Digit5", static_cast<int>(Key::Digit5)},
        {"Digit6", static_cast<int>(Key::Digit6)},
        {"Digit7", static_cast<int>(Key::Digit7)},
        {"Digit8", static_cast<int>(Key::Digit8)},
        {"Digit9", static_cast<int>(Key::Digit9)},
        {"Semicolon", static_cast<int>(Key::Semicolon)},
        {"Equal", static_cast<int>(Key::Equal)},
        {"A", static_cast<int>(Key::A)},
        {"B", static_cast<int>(Key::B)},
        {"C", static_cast<int>(Key::C)},
        {"D", static_cast<int>(Key::D)},
        {"E", static_cast<int>(Key::E)},
        {"F", static_cast<int>(Key::F)},
        {"G", static_cast<int>(Key::G)},
        {"H", static_cast<int>(Key::H)},
        {"I", static_cast<int>(Key::I)},
        {"J", static_cast<int>(Key::J)},
        {"K", static_cast<int>(Key::K)},
        {"L", static_cast<int>(Key::L)},
        {"M", static_cast<int>(Key::M)},
        {"N", static_cast<int>(Key::N)},
        {"O", static_cast<int>(Key::O)},
        {"P", static_cast<int>(Key::P)},
        {"Q", static_cast<int>(Key::Q)},
        {"R", static_cast<int>(Key::R)},
        {"S", static_cast<int>(Key::S)},
        {"T", static_cast<int>(Key::T)},
        {"U", static_cast<int>(Key::U)},
        {"V", static_cast<int>(Key::V)},
        {"W", static_cast<int>(Key::W)},
        {"X", static_cast<int>(Key::X)},
        {"Y", static_cast<int>(Key::Y)},
        {"Z", static_cast<int>(Key::Z)},
        {"LeftBracket", static_cast<int>(Key::LeftBracket)},
        {"Backslash", static_cast<int>(Key::Backslash)},
        {"RightBracket", static_cast<int>(Key::RightBracket)},
        {"GraveAccent", static_cast<int>(Key::GraveAccent)},
        {"World1", static_cast<int>(Key::World1)},
        {"World2", static_cast<int>(Key::World2)},
        {"Escape", static_cast<int>(Key::Escape)},
        {"Enter", static_cast<int>(Key::Enter)},
        {"Tab", static_cast<int>(Key::Tab)},
        {"Backspace", static_cast<int>(Key::Backspace)},
        {"Insert", static_cast<int>(Key::Insert)},
        {"Delete", static_cast<int>(Key::Delete)},
        {"Right", static_cast<int>(Key::Right)},
        {"Left", static_cast<int>(Key::Left)},
        {"Down", static_cast<int>(Key::Down)},
        {"Up", static_cast<int>(Key::Up)},
        {"PageUp", static_cast<int>(Key::PageUp)},
        {"PageDown", static_cast<int>(Key::PageDown)},
        {"Home", static_cast<int>(Key::Home)},
        {"End", static_cast<int>(Key::End)},
        {"CapsLock", static_cast<int>(Key::CapsLock)},
        {"ScrollLock", static_cast<int>(Key::ScrollLock)},
        {"NumLock", static_cast<int>(Key::NumLock)},
        {"PrintScreen", static_cast<int>(Key::PrintScreen)},
        {"Pause", static_cast<int>(Key::Pause)},
        {"F1", static_cast<int>(Key::F1)},
        {"F2", static_cast<int>(Key::F2)},
        {"F3", static_cast<int>(Key::F3)},
        {"F4", static_cast<int>(Key::F4)},
        {"F5", static_cast<int>(Key::F5)},
        {"F6", static_cast<int>(Key::F6)},
        {"F7", static_cast<int>(Key::F7)},
        {"F8", static_cast<int>(Key::F8)},
        {"F9", static_cast<int>(Key::F9)},
        {"F10", static_cast<int>(Key::F10)},
        {"F11", static_cast<int>(Key::F11)},
        {"F12", static_cast<int>(Key::F12)},
        {"F13", static_cast<int>(Key::F13)},
        {"F14", static_cast<int>(Key::F14)},
        {"F15", static_cast<int>(Key::F15)},
        {"F16", static_cast<int>(Key::F16)},
        {"F17", static_cast<int>(Key::F17)},
        {"F18", static_cast<int>(Key::F18)},
        {"F19", static_cast<int>(Key::F19)},
        {"F20", static_cast<int>(Key::F20)},
        {"F21", static_cast<int>(Key::F21)},
        {"F22", static_cast<int>(Key::F22)},
        {"F23", static_cast<int>(Key::F23)},
        {"F24", static_cast<int>(Key::F24)},
        {"F25", static_cast<int>(Key::F25)},
        {"Keypad0", static_cast<int>(Key::Keypad0)},
        {"Keypad1", static_cast<int>(Key::Keypad1)},
        {"Keypad2", static_cast<int>(Key::Keypad2)},
        {"Keypad3", static_cast<int>(Key::Keypad3)},
        {"Keypad4", static_cast<int>(Key::Keypad4)},
        {"Keypad5", static_cast<int>(Key::Keypad5)},
        {"Keypad6", static_cast<int>(Key::Keypad6)},
        {"Keypad7", static_cast<int>(Key::Keypad7)},
        {"Keypad8", static_cast<int>(Key::Keypad8)},
        {"Keypad9", static_cast<int>(Key::Keypad9)},
        {"KeypadDecimal", static_cast<int>(Key::KeypadDecimal)},
        {"KeypadDivide", static_cast<int>(Key::KeypadDivide)},
        {"KeypadMultiply", static_cast<int>(Key::KeypadMultiply)},
        {"KeypadSubtract", static_cast<int>(Key::KeypadSubtract)},
        {"KeypadAdd", static_cast<int>(Key::KeypadAdd)},
        {"KeypadEnter", static_cast<int>(Key::KeypadEnter)},
        {"KeypadEqual", static_cast<int>(Key::KeypadEqual)},
        {"LeftShift", static_cast<int>(Key::LeftShift)},
        {"LeftControl", static_cast<int>(Key::LeftControl)},
        {"LeftAlt", static_cast<int>(Key::LeftAlt)},
        {"LeftSuper", static_cast<int>(Key::LeftSuper)},
        {"RightShift", static_cast<int>(Key::RightShift)},
        {"RightControl", static_cast<int>(Key::RightControl)},
        {"RightAlt", static_cast<int>(Key::RightAlt)},
        {"RightSuper", static_cast<int>(Key::RightSuper)},
        {"Menu", static_cast<int>(Key::Menu)},
    };
    pushNamedCodes(state, kKeys, sizeof(kKeys) / sizeof(kKeys[0]));
    lua_setfield(state, -2, "Key");

    constexpr NamedCode kMouseButtons[] = {
        {"Left", static_cast<int>(MouseButton::Left)},
        {"Right", static_cast<int>(MouseButton::Right)},
        {"Middle", static_cast<int>(MouseButton::Middle)},
        {"Button4", static_cast<int>(MouseButton::Button4)},
        {"Button5", static_cast<int>(MouseButton::Button5)},
        {"Button6", static_cast<int>(MouseButton::Button6)},
        {"Button7", static_cast<int>(MouseButton::Button7)},
        {"Button8", static_cast<int>(MouseButton::Button8)},
    };
    pushNamedCodes(state, kMouseButtons, sizeof(kMouseButtons) / sizeof(kMouseButtons[0]));
    lua_setfield(state, -2, "MouseButton");
}

} // namespace sat::lua
