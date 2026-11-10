#include "LuaSceneBindings.hpp"

#include "LuaBindingUtils.hpp"

#include "Scene.hpp"

#include <lua.hpp>

#include <algorithm>
#include <limits>

namespace sat::lua {
namespace {

int createEntity(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const EntityId id = sceneFor(state).createEntity(name).id();
    lua_pushinteger(state, static_cast<lua_Integer>(id));
    return 1;
}

int createCube(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    Entity& entity = sceneFor(state).createEntity(name);
    entity.hasMesh = true;
    lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
    return 1;
}

int createCamera(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    Entity& entity = sceneFor(state).createEntity(name);
    entity.hasCamera = true;
    lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
    return 1;
}

int findEntity(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    const auto& entities = sceneFor(state).entities();
    for (std::size_t index = 0; index < entities.size(); ++index) {
        if (entities[index].name == name) {
            lua_pushinteger(state, static_cast<lua_Integer>(entities[index].id()));
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

int destroyEntity(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    lua_pushboolean(state, id > 0 && sceneFor(state).destroyEntity(static_cast<EntityId>(id)));
    return 1;
}

int isEntityValid(lua_State* state) {
    const lua_Integer id = luaL_checkinteger(state, 1);
    lua_pushboolean(state, id > 0 && sceneFor(state).entityById(static_cast<EntityId>(id)) != nullptr);
    return 1;
}

int getName(lua_State* state) {
    const auto& name = checkedEntity(state).name;
    lua_pushlstring(state, name.data(), name.size());
    return 1;
}

int getEntityCount(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(sceneFor(state).entityCount()));
    return 1;
}

int getEntities(lua_State* state) {
    const auto& entities = sceneFor(state).entities();
    const auto capacity = std::min(entities.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    lua_createtable(state, static_cast<int>(capacity), 0);
    lua_Integer index = 1;
    for (const auto& entity : entities) {
        lua_pushinteger(state, static_cast<lua_Integer>(entity.id()));
        lua_rawseti(state, -2, index++);
    }
    return 1;
}

template<Vec3 Transform::* member>
int getVector(lua_State* state) {
    const Vec3 value = checkedEntity(state).transform.*member;
    lua_pushnumber(state, value.x);
    lua_pushnumber(state, value.y);
    lua_pushnumber(state, value.z);
    return 3;
}

template<Vec3 Transform::* member>
int setVector(lua_State* state) {
    Entity& entity = checkedEntity(state);
    const float x = checkedFloat(state, 2);
    const float y = checkedFloat(state, 3);
    const float z = checkedFloat(state, 4);
    entity.transform.*member = {x, y, z};
    return 0;
}

int getPosition(lua_State* state) {
    const DVec3 value = checkedEntity(state).transform.position;
    lua_pushnumber(state, value.x);
    lua_pushnumber(state, value.y);
    lua_pushnumber(state, value.z);
    return 3;
}

int setPosition(lua_State* state) {
    Entity& entity = checkedEntity(state);
    entity.transform.position = {
        checkedDouble(state, 2),
        checkedDouble(state, 3),
        checkedDouble(state, 4),
    };
    return 0;
}

CameraComponent& checkedCamera(lua_State* state) {
    Entity& entity = checkedEntity(state);
    if (!entity.hasCamera) {
        luaL_error(state, "entity has no camera");
    }
    return entity.camera;
}

int getMainCamera(lua_State* state) {
    const Scene& scene = sceneFor(state);
    const int index = scene.mainCameraIndex();
    if (index < 0) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, static_cast<lua_Integer>(scene.entity(static_cast<std::size_t>(index))->id()));
    }
    return 1;
}

int getCameraFov(lua_State* state) {
    lua_pushnumber(state, checkedCamera(state).fieldOfView);
    return 1;
}

int setCameraFov(lua_State* state) {
    CameraComponent& camera = checkedCamera(state);
    const float fov = checkedFloat(state, 2);
    if (fov <= 0.0f || fov >= 180.0f) {
        return luaL_argerror(state, 2, "vertical FOV must be greater than 0 and less than 180 degrees");
    }
    camera.fieldOfView = fov;
    return 0;
}

int getCameraClipPlanes(lua_State* state) {
    const CameraComponent& camera = checkedCamera(state);
    lua_pushnumber(state, camera.nearClip);
    lua_pushnumber(state, camera.farClip);
    return 2;
}

int setCameraClipPlanes(lua_State* state) {
    CameraComponent& camera = checkedCamera(state);
    const float nearClip = checkedFloat(state, 2);
    const float farClip = checkedFloat(state, 3);
    if (nearClip <= 0.0f) {
        return luaL_argerror(state, 2, "near clip must be greater than 0");
    }
    if (farClip <= nearClip) {
        return luaL_argerror(state, 3, "far clip must be greater than near clip");
    }
    // Validate float-converted values before committing either plane.
    camera.nearClip = nearClip;
    camera.farClip = farClip;
    return 0;
}

int getCameraClearColor(lua_State* state) {
    const CameraComponent& camera = checkedCamera(state);
    for (float channel : camera.clearColor) {
        lua_pushnumber(state, channel);
    }
    return 4;
}

int setCameraClearColor(lua_State* state) {
    CameraComponent& camera = checkedCamera(state);
    const float red = checkedFloat(state, 2);
    const float green = checkedFloat(state, 3);
    const float blue = checkedFloat(state, 4);
    const float alpha = checkedFloat(state, 5);
    // Match scene-file validation: finite RGBA floats, without implicit clamping.
    camera.clearColor[0] = red;
    camera.clearColor[1] = green;
    camera.clearColor[2] = blue;
    camera.clearColor[3] = alpha;
    return 0;
}

int setMainCamera(lua_State* state) {
    const EntityId id = checkedEntity(state).id();
    if (!sceneFor(state).setMainCameraById(id)) {
        return luaL_error(state, "entity has no camera");
    }
    return 0;
}

template<bool Entity::* flag>
int hasComponent(lua_State* state) {
    lua_pushboolean(state, checkedEntity(state).*flag);
    return 1;
}

template<bool Entity::* flag, auto Entity::* component>
int addComponent(lua_State* state) {
    Entity& entity = checkedEntity(state);
    if (entity.*flag) {
        lua_pushboolean(state, false);
        return 1;
    }
    entity.*component = {};
    entity.*flag = true;
    lua_pushboolean(state, true);
    return 1;
}

template<bool Entity::* flag, auto Entity::* component>
int removeComponent(lua_State* state) {
    Entity& entity = checkedEntity(state);
    if (!(entity.*flag)) {
        lua_pushboolean(state, false);
        return 1;
    }
    entity.*flag = false;
    entity.*component = {};
    lua_pushboolean(state, true);
    return 1;
}

ScriptComponent* findScript(Entity& entity, std::string_view path) {
    for (auto& script : entity.scripts) {
        if (script.path == path) {
            return &script;
        }
    }
    return nullptr;
}

int isScriptEnabled(lua_State* state) {
    Entity& entity = checkedEntity(state);
    const char* path = luaL_checkstring(state, 2);
    const ScriptComponent* script = findScript(entity, path);
    lua_pushboolean(state, script != nullptr && script->enabled);
    return 1;
}

int setScriptEnabled(lua_State* state) {
    Entity& entity = checkedEntity(state);
    const char* path = luaL_checkstring(state, 2);
    luaL_checktype(state, 3, LUA_TBOOLEAN);
    ScriptComponent* script = findScript(entity, path);
    if (script == nullptr) {
        lua_pushboolean(state, false);
        return 1;
    }
    script->enabled = lua_toboolean(state, 3) != 0;
    lua_pushboolean(state, true);
    return 1;
}

} // namespace

void registerSceneBindings(lua_State* state, Scene& scene) {
    const luaL_Reg bindings[] = {
        // @lua fun sat.createEntity(name: string): EntityId
        {"createEntity", guarded<createEntity>},
        // @lua fun sat.createCube(name: string): EntityId
        {"createCube", guarded<createCube>},
        // @lua fun sat.createCamera(name: string): EntityId
        {"createCamera", guarded<createCamera>},
        // @lua fun sat.findEntity(name: string): EntityId|nil
        {"findEntity", guarded<findEntity>},
        // @lua fun sat.destroyEntity(entity: EntityId): boolean
        {"destroyEntity", guarded<destroyEntity>},
        // @lua fun sat.isEntityValid(entity: EntityId): boolean
        {"isEntityValid", guarded<isEntityValid>},
        // @lua fun sat.getName(entity: EntityId): string
        {"getName", guarded<getName>},
        // @lua fun sat.getEntities(): EntityId[]
        {"getEntities", guarded<getEntities>},
        // @lua fun sat.getEntityCount(): integer
        {"getEntityCount", guarded<getEntityCount>},
        // @lua fun sat.getPosition(entity: EntityId): x: number, y: number, z: number
        {"getPosition", guarded<getPosition>},
        // @lua fun sat.setPosition(entity: EntityId, x: number, y: number, z: number)
        {"setPosition", guarded<setPosition>},
        // @lua fun sat.getRotation(entity: EntityId): x: number, y: number, z: number
        {"getRotation", guarded<getVector<&Transform::rotation>>},
        // @lua fun sat.setRotation(entity: EntityId, x: number, y: number, z: number)
        {"setRotation", guarded<setVector<&Transform::rotation>>},
        // @lua fun sat.getScale(entity: EntityId): x: number, y: number, z: number
        {"getScale", guarded<getVector<&Transform::scale>>},
        // @lua fun sat.setScale(entity: EntityId, x: number, y: number, z: number)
        {"setScale", guarded<setVector<&Transform::scale>>},
        // @lua fun sat.setMainCamera(entity: EntityId)
        {"setMainCamera", guarded<setMainCamera>},
        // @lua fun sat.getMainCamera(): EntityId|nil
        {"getMainCamera", guarded<getMainCamera>},
        // @lua fun sat.getCameraFov(entity: EntityId): number
        {"getCameraFov", guarded<getCameraFov>},
        // @lua fun sat.setCameraFov(entity: EntityId, fov: number)
        {"setCameraFov", guarded<setCameraFov>},
        // @lua fun sat.getCameraClipPlanes(entity: EntityId): nearClip: number, farClip: number
        {"getCameraClipPlanes", guarded<getCameraClipPlanes>},
        // @lua fun sat.setCameraClipPlanes(entity: EntityId, nearClip: number, farClip: number)
        {"setCameraClipPlanes", guarded<setCameraClipPlanes>},
        // @lua fun sat.getCameraClearColor(entity: EntityId): r: number, g: number, b: number, a: number
        {"getCameraClearColor", guarded<getCameraClearColor>},
        // @lua fun sat.setCameraClearColor(entity: EntityId, r: number, g: number, b: number, a: number)
        {"setCameraClearColor", guarded<setCameraClearColor>},
        // @lua fun sat.hasMesh(entity: EntityId): boolean
        {"hasMesh", guarded<hasComponent<&Entity::hasMesh>>},
        // @lua fun sat.hasCamera(entity: EntityId): boolean
        {"hasCamera", guarded<hasComponent<&Entity::hasCamera>>},
        // @lua fun sat.addMesh(entity: EntityId): boolean
        {"addMesh", guarded<addComponent<&Entity::hasMesh, &Entity::mesh>>},
        // @lua fun sat.removeMesh(entity: EntityId): boolean
        {"removeMesh", guarded<removeComponent<&Entity::hasMesh, &Entity::mesh>>},
        // @lua fun sat.addCamera(entity: EntityId): boolean
        {"addCamera", guarded<addComponent<&Entity::hasCamera, &Entity::camera>>},
        // @lua fun sat.removeCamera(entity: EntityId): boolean
        {"removeCamera", guarded<removeComponent<&Entity::hasCamera, &Entity::camera>>},
        // @lua fun sat.isScriptEnabled(entity: EntityId, path: string): boolean
        {"isScriptEnabled", guarded<isScriptEnabled>},
        // @lua fun sat.setScriptEnabled(entity: EntityId, path: string, enabled: boolean): boolean
        {"setScriptEnabled", guarded<setScriptEnabled>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &scene);
    luaL_setfuncs(state, bindings, 1);
}

} // namespace sat::lua
