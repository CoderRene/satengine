#include "LuaUiBindings.hpp"

#include "LuaBindingUtils.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <string_view>

#include <utility>
#include <vector>

namespace sat::lua {
namespace {

constexpr char kDocumentType[] = "sat.UiDocument";
constexpr char kElementType[] = "sat.UiElement";
char callbackTableKey;

GameUi* uiFor(lua_State* state) {
    return static_cast<GameUi*>(lua_touserdata(state, lua_upvalueindex(1)));
}

GameUi& checkedUi(lua_State* state) {
    auto* ui = uiFor(state);
    if (!ui || !ui->valid()) { luaL_error(state, "game UI is not available"); }
    return *ui;
}

UiOwner ownerFor(lua_State* state) {
    return reinterpret_cast<UiOwner>(lua_touserdata(state, lua_upvalueindex(2)));
}

std::string_view checkedString(lua_State* state, int index) {
    std::size_t size = 0;
    const char* text = luaL_checklstring(state, index, &size);
    return {text, size};
}

template<class Handle>
void pushHandle(lua_State* state, Handle handle, const char* type) {
    *static_cast<Handle*>(lua_newuserdatauv(state, sizeof(Handle), 0)) = handle;
    luaL_setmetatable(state, type);
}

UiElement checkedElement(lua_State* state) {
    return *static_cast<const UiElement*>(luaL_checkudata(state, 1, kElementType));
}

UiDocument checkedDocument(lua_State* state) {
    return *static_cast<const UiDocument*>(luaL_checkudata(state, 1, kDocumentType));
}

int loadFont(lua_State* state) {
    lua_pushboolean(state, checkedUi(state).loadFont(checkedString(state, 1)));
    return 1;
}

int load(lua_State* state) {
    const auto path = checkedString(state, 1);
    auto* ui = uiFor(state);
    if (!ui || !ui->valid()) {
        lua_pushnil(state);
        lua_pushliteral(state, "game UI is not available");
        return 2;
    }
    const auto document = ui->loadDocument(path);
    if (!document.generation) {
        lua_pushnil(state);
        lua_pushfstring(state, "could not load UI document '%s'", path.data());
        return 2;
    }
    pushHandle(state, document, kDocumentType);
    return 1;
}

template<bool (GameUi::*Operation)(UiDocument)>
int documentOperation(lua_State* state) {
    lua_pushboolean(state, (checkedUi(state).*Operation)(checkedDocument(state)));
    return 1;
}

int element(lua_State* state) {
    const auto handle = checkedUi(state).element(checkedDocument(state), checkedString(state, 2));
    if (handle.generation) {
        pushHandle(state, handle, kElementType);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

template<bool (GameUi::*Operation)(UiElement, std::string_view)>
int elementString(lua_State* state) {
    lua_pushboolean(state, (checkedUi(state).*Operation)(checkedElement(state), checkedString(state, 2)));
    return 1;
}

template<bool (GameUi::*Operation)(UiElement, std::string_view, std::string_view)>
int elementStrings(lua_State* state) {
    const auto handle = checkedElement(state);
    const auto name = checkedString(state, 2);
    const auto value = checkedString(state, 3);
    lua_pushboolean(state, (checkedUi(state).*Operation)(handle, name, value));
    return 1;
}

int setClass(lua_State* state) {
    luaL_checktype(state, 3, LUA_TBOOLEAN);
    lua_pushboolean(state, checkedUi(state).setClass(checkedElement(state),
        checkedString(state, 2), lua_toboolean(state, 3) != 0));
    return 1;
}

int getValue(lua_State* state) {
    const auto value = checkedUi(state).value(checkedElement(state));
    if (value) {
        lua_pushlstring(state, value->data(), value->size());
    } else {
        lua_pushnil(state);
    }
    return 1;
}

int on(lua_State* state) {
    luaL_checktype(state, 3, LUA_TFUNCTION);
    const auto listener = checkedUi(state).subscribe(checkedElement(state), checkedString(state, 2), ownerFor(state));
    if (!listener) {
        lua_pushnil(state);
        return 1;
    }
    lua_rawgetp(state, LUA_REGISTRYINDEX, &callbackTableKey);
    lua_pushvalue(state, 3);
    lua_rawseti(state, -2, static_cast<lua_Integer>(listener));
    lua_pop(state, 1);
    lua_pushinteger(state, static_cast<lua_Integer>(listener));
    return 1;
}

int off(lua_State* state) {
    const auto listener = luaL_checkinteger(state, 1);
    const bool removed = checkedUi(state).unsubscribe(static_cast<UiListener>(listener), ownerFor(state));
    if (removed) {
        lua_rawgetp(state, LUA_REGISTRYINDEX, &callbackTableKey);
        lua_pushnil(state);
        lua_rawseti(state, -2, listener);
        lua_pop(state, 1);
    }
    lua_pushboolean(state, removed);
    return 1;
}

bool readModelValue(lua_State* state, int index, UiDataValue& value) {
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        value = lua_toboolean(state, index) != 0;
        return true;
    case LUA_TNUMBER: {
        const double number = lua_tonumber(state, index);
        if (!std::isfinite(number)) { return false; }
        value = number;
        return true;
    }
    case LUA_TSTRING:
        value = std::string(checkedString(state, index));
        return true;
    default:
        return false;
    }
}

int createModel(lua_State* state) {
    auto& ui = checkedUi(state);
    const auto name = checkedString(state, 1);
    luaL_checktype(state, 2, LUA_TTABLE);
    std::vector<UiDataField> fields;
    lua_pushnil(state);
    while (lua_next(state, 2) != 0) {
        UiDataValue value;
        if (lua_type(state, -2) != LUA_TSTRING || !readModelValue(state, -1, value)) {
            lua_pop(state, 2);
            lua_pushboolean(state, false);
            return 1;
        }
        fields.emplace_back(checkedString(state, -2), std::move(value));
        lua_pop(state, 1);
    }
    lua_pushboolean(state, ui.createModel(name, fields, ownerFor(state)));
    return 1;
}

int setModelValue(lua_State* state) {
    auto& ui = checkedUi(state);
    const auto model = checkedString(state, 1);
    const auto field = checkedString(state, 2);
    UiDataValue value;
    lua_pushboolean(state, readModelValue(state, 3, value) && ui.setModelValue(model, field, std::move(value)));
    return 1;
}

int getModelValue(lua_State* state) {
    const auto value = checkedUi(state).modelValue(checkedString(state, 1), checkedString(state, 2));
    if (!value) {
        lua_pushnil(state);
    } else {
        std::visit([state](const auto& stored) {
            using Value = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<Value, bool>) { lua_pushboolean(state, stored); }
            else if constexpr (std::is_same_v<Value, double>) { lua_pushnumber(state, stored); }
            else { lua_pushlstring(state, stored.data(), stored.size()); }
        }, *value);
    }
    return 1;
}

int getDocument(lua_State* state) {
    auto& ui = checkedUi(state);
    const auto id = luaL_checkinteger(state, 1);
    const auto* scene = static_cast<const Scene*>(lua_touserdata(state, lua_upvalueindex(3)));
    const auto handle = id > 0 ? ui.sceneDocument(*scene, static_cast<EntityId>(id)) : UiDocument{};
    if (handle.generation) { pushHandle(state, handle, kDocumentType); }
    else { lua_pushnil(state); }
    return 1;
}

template<bool (GameUi::*Operation)() const>
int wantsInput(lua_State* state) {
    const auto* ui = uiFor(state);
    lua_pushboolean(state, ui && (ui->*Operation)());
    return 1;
}

} // namespace

void registerUiBindings(lua_State* state, GameUi* ui, UiOwner owner, const Scene& scene) {
    for (const auto* type : {kDocumentType, kElementType}) {
        luaL_newmetatable(state, type);
        lua_pushliteral(state, "opaque UI handle");
        lua_setfield(state, -2, "__metatable");
        lua_pop(state, 1);
    }
    lua_newtable(state);
    lua_rawsetp(state, LUA_REGISTRYINDEX, &callbackTableKey);

    const luaL_Reg bindings[] = {
        // @lua fun sat.ui.createModel(name: string, fields: table): boolean
        {"createModel", guarded<createModel>},
        // @lua fun sat.ui.setModelValue(model: string, field: string, value: boolean|number|string): boolean
        {"setModelValue", guarded<setModelValue>},
        // @lua fun sat.ui.getModelValue(model: string, field: string): boolean|number|string|nil
        {"getModelValue", guarded<getModelValue>},
        // @lua fun sat.ui.getDocument(entity: EntityId): userdata|nil
        {"getDocument", guarded<getDocument>},
        // @lua fun sat.ui.loadFont(path: string): boolean
        {"loadFont", guarded<loadFont>},
        // @lua fun sat.ui.load(path: string): userdata|nil, string|nil
        {"load", guarded<load>},
        // @lua fun sat.ui.show(doc: userdata): boolean
        {"show", guarded<documentOperation<&GameUi::show>>},
        // @lua fun sat.ui.hide(doc: userdata): boolean
        {"hide", guarded<documentOperation<&GameUi::hide>>},
        // @lua fun sat.ui.close(doc: userdata): boolean
        {"close", guarded<documentOperation<&GameUi::close>>},
        // @lua fun sat.ui.element(doc: userdata, id: string): userdata|nil
        {"element", guarded<element>},
        // @lua fun sat.ui.setText(element: userdata, text: string): boolean
        {"setText", guarded<elementString<&GameUi::setText>>},
        // @lua fun sat.ui.setRml(element: userdata, markup: string): boolean
        {"setRml", guarded<elementString<&GameUi::setRml>>},
        // @lua fun sat.ui.setClass(element: userdata, name: string, enabled: boolean): boolean
        {"setClass", guarded<setClass>},
        // @lua fun sat.ui.setProperty(element: userdata, name: string, value: string): boolean
        {"setProperty", guarded<elementStrings<&GameUi::setProperty>>},
        // @lua fun sat.ui.removeProperty(element: userdata, name: string): boolean
        {"removeProperty", guarded<elementString<&GameUi::removeProperty>>},
        // @lua fun sat.ui.setAttribute(element: userdata, name: string, value: string): boolean
        {"setAttribute", guarded<elementStrings<&GameUi::setAttribute>>},
        // @lua fun sat.ui.getValue(element: userdata): string|nil
        {"getValue", guarded<getValue>},
        // @lua fun sat.ui.on(element: userdata, eventName: string, callback: function): integer|nil
        {"on", guarded<on>},
        // @lua fun sat.ui.off(listener: integer): boolean
        {"off", guarded<off>},
        // @lua fun sat.ui.wantsMouse(): boolean
        {"wantsMouse", wantsInput<&GameUi::wantsMouse>},
        // @lua fun sat.ui.wantsKeyboard(): boolean
        {"wantsKeyboard", wantsInput<&GameUi::wantsKeyboard>},
        {nullptr, nullptr}
    };
    lua_newtable(state);
    lua_pushlightuserdata(state, ui);
    lua_pushlightuserdata(state, reinterpret_cast<void*>(owner));
    lua_pushlightuserdata(state, const_cast<Scene*>(&scene));
    luaL_setfuncs(state, bindings, 3);
    lua_setfield(state, -2, "ui");
}

bool pushUiCallback(lua_State* state, const UiEvent& event) {
    lua_rawgetp(state, LUA_REGISTRYINDEX, &callbackTableKey);
    const int type = lua_rawgeti(state, -1, static_cast<lua_Integer>(event.listener));
    lua_remove(state, -2);
    if (type != LUA_TFUNCTION) {
        lua_pop(state, 1);
        return false;
    }

    lua_createtable(state, 0, 5);
    lua_pushlstring(state, event.type.data(), event.type.size());
    lua_setfield(state, -2, "type");
    lua_pushinteger(state, event.x);
    lua_setfield(state, -2, "x");
    lua_pushinteger(state, event.y);
    lua_setfield(state, -2, "y");
    lua_pushinteger(state, event.key);
    lua_setfield(state, -2, "key");
    lua_pushlstring(state, event.value.data(), event.value.size());
    lua_setfield(state, -2, "value");
    return true;
}

void pruneUiCallbacks(lua_State* state, const GameUi& ui, UiOwner owner) {
    lua_rawgetp(state, LUA_REGISTRYINDEX, &callbackTableKey);
    lua_pushnil(state);
    while (lua_next(state, -2) != 0) {
        lua_pop(state, 1);
        const auto key = lua_tointeger(state, -1);
        if (!ui.subscribed(static_cast<UiListener>(key), owner)) {
            lua_pushnil(state);
            lua_rawseti(state, -3, key);
        }
    }
    lua_pop(state, 1);
}

} // namespace sat::lua
