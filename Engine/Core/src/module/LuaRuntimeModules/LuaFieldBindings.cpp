#include "LuaFieldBindings.hpp"

#include "LuaBindingUtils.hpp"
#include "LuaScriptFields.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sat::lua {
namespace {

void setField(lua_State* state, const std::string& name, const ScriptFieldValue& value) {
    lua_pushlstring(state, name.data(), name.size());
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            lua_pushboolean(state, item);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            lua_pushinteger(state, item);
        } else if constexpr (std::is_same_v<T, double>) {
            lua_pushnumber(state, item);
        } else if constexpr (std::is_same_v<T, ScriptEntityReference>) {
            if (item.entityId == kInvalidEntityId) {
                lua_pushnil(state);
            } else {
                lua_pushinteger(state, static_cast<lua_Integer>(item.entityId));
            }
        } else {
            lua_pushlstring(state, item.data(), item.size());
        }
    }, value);
    lua_rawset(state, -3);
}

char entityFieldTag;

int entityField(lua_State* state) {
    lua_pushlightuserdata(state, &entityFieldTag);
    return 1;
}

void readDefaults(lua_State* state, ScriptFields& defaults) {
    lua_pushnil(state);
    while (lua_next(state, 1) != 0) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            luaL_argerror(state, 1, "expected named fields");
        }
        std::size_t nameSize = 0;
        const char* name = lua_tolstring(state, -2, &nameSize);
        if (!isValidScriptFieldName({name, nameSize})) {
            luaL_argerror(state, 1, "invalid field name (entity is reserved)");
        }
        if (defaults.size() >= kMaxScriptFields) {
            luaL_argerror(state, 1, "too many exposed fields");
        }

        switch (lua_type(state, -1)) {
        case LUA_TBOOLEAN:
            defaults.emplace(std::string(name, nameSize), lua_toboolean(state, -1) != 0);
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(state, -1)) {
                defaults.emplace(std::string(name, nameSize), static_cast<std::int64_t>(lua_tointeger(state, -1)));
            } else {
                const double value = lua_tonumber(state, -1);
                if (!std::isfinite(value)) {
                    luaL_argerror(state, 1, "expected a finite field number");
                }
                defaults.emplace(std::string(name, nameSize), value);
            }
            break;
        case LUA_TSTRING: {
            std::size_t size = 0;
            const char* value = lua_tolstring(state, -1, &size);
            if (!isValidScriptFieldText({value, size})) {
                luaL_argerror(state, 1, "expected single-line field text up to 4096 bytes");
            }
            defaults.emplace(std::string(name, nameSize), std::string(value, size));
            break;
        }
        case LUA_TLIGHTUSERDATA:
            if (lua_touserdata(state, -1) == &entityFieldTag) {
                defaults.emplace(std::string(name, nameSize), ScriptEntityReference{});
                break;
            }
            [[fallthrough]];
        default:
            luaL_argerror(state, 1, "expected a boolean, integer, number, string or sat.entityField()");
        }
        lua_pop(state, 1);
    }
}

const ScriptFieldValue* matchingOverride(const ScriptFields& overrides, const std::string& name,
                                         const ScriptFieldValue& defaultValue) {
    const auto found = overrides.find(name);
    return found != overrides.end() && found->second.index() == defaultValue.index() ? &found->second : nullptr;
}

int expose(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    auto& fields = *static_cast<FieldBindings*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (!fields.m_accepting || fields.m_declared) {
        return luaL_error(state, "sat.expose may be called once during script initialization");
    }
    // Keep owned C++ values in the binding across Lua validation errors.
    fields.m_defaults.clear();
    readDefaults(state, fields.m_defaults);
    if (fields.m_discovery) {
        // Finish discovery before control returns to the script.
        lua_pushlightuserdata(state, &fields);
        return lua_error(state);
    }

    lua_pushglobaltable(state);
    lua_pushliteral(state, "self");
    lua_rawget(state, -2);
    if (!lua_istable(state, -1)) {
        return luaL_error(state, "sat.expose requires the component self table");
    }
    for (const auto& [name, defaultValue] : fields.m_defaults) {
        const auto* value = matchingOverride(*fields.m_startupOverrides, name, defaultValue);
        setField(state, name, value ? *value : defaultValue);
    }
    fields.m_selfRef = luaL_ref(state, LUA_REGISTRYINDEX);
    fields.m_declared = true;
    lua_pop(state, 1);
    return 0;
}

} // namespace

void registerFieldBindings(lua_State* state, FieldBindings& fields) {
    static constexpr luaL_Reg kFunctions[] = {
        // @lua fun sat.expose(fields:table)
        {"expose", guarded<expose>},
        // @lua fun sat.entityField(): lightuserdata
        {"entityField", entityField},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &fields);
    luaL_setfuncs(state, kFunctions, 1);
}

void applyFieldEdits(lua_State* state, const FieldBindings& fields, const ScriptFields& before,
                     const ScriptFields& after) {
    if (!fields.m_declared || fields.m_defaults.empty()) {
        return;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, fields.m_selfRef);
    for (const auto& [name, defaultValue] : fields.m_defaults) {
        const auto* previous = matchingOverride(before, name, defaultValue);
        const auto* next = matchingOverride(after, name, defaultValue);
        if (next ? (!previous || *next != *previous) : previous != nullptr) {
            setField(state, name, next ? *next : defaultValue);
        }
    }
    lua_pop(state, 1);
}

} // namespace sat::lua

namespace sat {
namespace {

constexpr std::size_t kMaxDiscoveryBytes = 4 * 1024 * 1024;
constexpr std::string_view kLuaWhitespace = " \t\r\n\f\v";

std::size_t longStringLength(std::string_view source) {
    if (!source.starts_with('[')) {
        return 0;
    }
    const auto bracket = source.find_first_not_of('=', 1);
    if (bracket == std::string_view::npos || source[bracket] != '[') {
        return 0;
    }
    const std::string close = "]" + std::string(bracket - 1, '=') + "]";
    const auto end = source.find(close, bracket + 1);
    return end == std::string_view::npos ? source.size() : end + close.size();
}

std::string_view skipTrivia(std::string_view source) {
    // Skip Lua whitespace, line comments and long comments before the declaration.
    while (true) {
        const auto first = source.find_first_not_of(kLuaWhitespace);
        if (first == std::string_view::npos) {
            return source.substr(source.size());
        }
        source.remove_prefix(first);
        if (!source.starts_with("--")) {
            return source;
        }
        source.remove_prefix(2);
        if (const auto length = longStringLength(source); length != 0) {
            source.remove_prefix(length);
            continue;
        }
        const auto end = source.find_first_of("\r\n");
        if (end == std::string_view::npos) {
            return source.substr(source.size());
        }
        source.remove_prefix(end + 1);
    }
}

bool identifierCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string_view takeToken(std::string_view& source) {
    source = skipTrivia(source);
    if (source.empty()) {
        return source;
    }
    std::size_t length = 1;
    const char first = source.front();
    if (first == '"' || first == '\'') {
        while (length < source.size()) {
            const char c = source[length++];
            if (c == first) { break; }
            if (c == '\\' && length < source.size()) { ++length; }
        }
    } else if (const auto longLength = longStringLength(source); longLength != 0) {
        length = longLength;
    } else if (identifierCharacter(first)) {
        while (length < source.size() && identifierCharacter(source[length])) { ++length; }
    } else if (source.starts_with("==")) {
        length = 2;
    }
    const auto token = source.substr(0, length);
    source.remove_prefix(length);
    return token;
}

constexpr const char* kRecordFieldKey = "__sat_record_field_key";

// Lua table iteration has no declaration order. Instrument only the outer
// constructor in the discovery copy so Lua still evaluates keys and defaults.
std::string orderDiscoverySource(std::string_view source, std::string_view table) {
    std::string result;
    std::size_t copied = 0;
    int nested = 0;
    int blocks = 0;
    bool atField = true;
    bool bracketKey = false;
    const auto copyTo = [&](const char* position) {
        const auto offset = static_cast<std::size_t>(position - source.data());
        result.append(source.substr(copied, offset - copied));
        copied = offset;
    };
    for (auto token = takeToken(table); !token.empty(); token = takeToken(table)) {
        if (nested == 0 && blocks == 0) {
            if (token == "}") {
                break;
            }
            if (atField && token == "[") {
                copyTo(token.data() + 1);
                result.append(kRecordFieldKey).append("(");
                bracketKey = true;
            } else if (atField && isValidScriptFieldName(token)) {
                auto lookahead = table;
                if (takeToken(lookahead) == "=") {
                    copyTo(token.data());
                    result.append("[").append(kRecordFieldKey).append("(\"").append(token).append("\")]");
                    copied += token.size();
                }
            }
            atField = token == "," || token == ";";
        }
        if (token == "{" || token == "(" || token == "[") {
            ++nested;
        } else if (token == "}" || token == ")" || token == "]") {
            --nested;
            if (bracketKey && nested == 0 && token == "]") {
                copyTo(token.data());
                result += ')';
                bracketKey = false;
            }
        } else if (token == "function" || token == "if" || token == "do" || token == "repeat") {
            ++blocks;
        } else if (token == "end" || token == "until") {
            --blocks;
        }
    }
    result.append(source.substr(copied));
    return result;
}

int recordFieldKey(lua_State* state) {
    auto& names = *static_cast<std::vector<std::string>*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (lua_type(state, 1) == LUA_TSTRING) {
        std::size_t size = 0;
        const char* name = lua_tolstring(state, 1, &size);
        if (isValidScriptFieldName({name, size})) {
            names.emplace_back(name, size);
        }
    }
    lua_settop(state, 1);
    return 1;
}

void* allocateDiscovery(void* context, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto& used = *static_cast<std::size_t*>(context);
    if (pointer == nullptr) {
        oldSize = 0;
    }
    if (newSize == 0) {
        std::free(pointer);
        used -= oldSize;
        return nullptr;
    }
    if (newSize > kMaxDiscoveryBytes - (used - oldSize)) {
        return nullptr;
    }
    void* result = std::realloc(pointer, newSize);
    if (result != nullptr) {
        used = used - oldSize + newSize;
    }
    return result;
}

void limitDiscovery(lua_State* state, lua_Debug*) {
    luaL_error(state, "field declaration exceeded the instruction budget");
}

bool readScriptForDiscovery(const std::filesystem::path& scriptPath, std::string& source, std::string& error) {
    std::ifstream file(scriptPath, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (size < 0) {
        error = "Cannot read script.";
        return false;
    }
    if (size > static_cast<std::streamoff>(kMaxDiscoveryBytes)) {
        error = "Script exceeds the 4 MiB field discovery limit.";
        return false;
    }
    source.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(source.data(), static_cast<std::streamsize>(source.size()))) {
        error = "Cannot read script.";
        return false;
    }
    return true;
}

bool inspectSource(std::string_view source, ScriptFields& fields, std::string& error,
                   std::vector<std::string>* fieldOrder = nullptr) {
    auto declaration = skipTrivia(source);
    if (!declaration.starts_with("sat.expose")) {
        return true;
    }
    declaration.remove_prefix(std::string_view("sat.expose").size());
    auto token = takeToken(declaration);
    while (token == "(") {
        token = takeToken(declaration);
    }
    std::vector<std::string> names;
    std::string orderedSource;
    if (fieldOrder && token == "{") {
        orderedSource = orderDiscoverySource(source, declaration);
    }

    std::size_t used = 0;
    lua::FieldBindings binding;
    binding.m_discovery = true;
    const std::unique_ptr<lua_State, decltype(&lua_close)> vm(lua_newstate(allocateDiscovery, &used), lua_close);
    auto* state = vm.get();
    if (state == nullptr) {
        error = "Cannot allocate Lua field discovery state.";
        return false;
    }
    // The project script sees only sat.expose and has no standard libraries or engine access.
    lua_newtable(state);
    lua::registerFieldBindings(state, binding);
    lua_setglobal(state, "sat");
    if (!orderedSource.empty()) {
        lua_pushlightuserdata(state, &names);
        lua_pushcclosure(state, lua::guarded<recordFieldKey>, 1);
        lua_setglobal(state, kRecordFieldKey);
    }
    lua_sethook(state, limitDiscovery, LUA_MASKCOUNT, 10000);
    // Validate the original syntax before running the instrumented chunk.
    int loaded = luaL_loadbufferx(state, source.data(), source.size(), "=Script fields", "t");
    if (loaded == LUA_OK && !orderedSource.empty()) {
        lua_pop(state, 1);
        loaded = luaL_loadbufferx(state, orderedSource.data(), orderedSource.size(), "=Script fields", "t");
    }
    if (loaded == LUA_OK) {
        const int result = lua_pcall(state, 0, 0, 0);
        if (result == LUA_OK) {
            return true;
        }
        if (lua_touserdata(state, -1) == &binding) {
            fields = std::move(binding.m_defaults);
            if (fieldOrder) {
                for (auto& name : names) {
                    if (fields.contains(name) && std::ranges::find(*fieldOrder, name) == fieldOrder->end()) {
                        fieldOrder->push_back(std::move(name));
                    }
                }
                for (const auto& [name, value] : fields) {
                    if (std::ranges::find(*fieldOrder, name) == fieldOrder->end()) {
                        fieldOrder->push_back(name);
                    }
                }
            }
            return true;
        }
    }
    const char* message = lua_tostring(state, -1);
    error = message ? message : "Cannot discover Lua script fields.";
    return false;
}

} // namespace

bool inspectLuaScriptFields(const std::filesystem::path& scriptPath, ScriptFields& fields, std::string& error) {
    fields.clear();
    error.clear();
    std::string source;
    return readScriptForDiscovery(scriptPath, source, error) && inspectSource(source, fields, error);
}

void LuaScriptFieldPreview::refresh(const std::filesystem::path& scriptPath) {
    std::string source;
    std::string error;
    if (!readScriptForDiscovery(scriptPath, source, error)) {
        m_source.reset();
        m_error = std::move(error);
        return;
    }
    if (m_source && *m_source == source) {
        return;
    }
    m_source = std::move(source);
    m_error.clear();
    ScriptFields defaults;
    std::vector<std::string> fieldOrder;
    if (inspectSource(*m_source, defaults, m_error, &fieldOrder)) {
        m_defaults = std::move(defaults);
        m_fieldOrder = std::move(fieldOrder);
    }
}

} // namespace sat
