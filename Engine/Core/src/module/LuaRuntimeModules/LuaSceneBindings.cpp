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

} // namespace

void registerSceneBindings(lua_State* state, Scene& scene) {
    const luaL_Reg bindings[] = {
        {"createEntity", guarded<createEntity>},
        {"createCube", guarded<createCube>},
        {"createCamera", guarded<createCamera>},
        {"findEntity", guarded<findEntity>},
        {"destroyEntity", guarded<destroyEntity>},
        {"isEntityValid", guarded<isEntityValid>},
        {"getName", guarded<getName>},
        {"getEntities", guarded<getEntities>},
        {"getEntityCount", guarded<getEntityCount>},
        {"getPosition", guarded<getVector<&Transform::position>>},
        {"setPosition", guarded<setVector<&Transform::position>>},
        {"getRotation", guarded<getVector<&Transform::rotation>>},
        {"setRotation", guarded<setVector<&Transform::rotation>>},
        {"getScale", guarded<getVector<&Transform::scale>>},
        {"setScale", guarded<setVector<&Transform::scale>>},
        {"setMainCamera", guarded<setMainCamera>},
        {"getMainCamera", guarded<getMainCamera>},
        {"getCameraFov", guarded<getCameraFov>},
        {"setCameraFov", guarded<setCameraFov>},
        {"getCameraClipPlanes", guarded<getCameraClipPlanes>},
        {"setCameraClipPlanes", guarded<setCameraClipPlanes>},
        {"getCameraClearColor", guarded<getCameraClearColor>},
        {"setCameraClearColor", guarded<setCameraClearColor>},
        {nullptr, nullptr}
    };
    lua_pushlightuserdata(state, &scene);
    luaL_setfuncs(state, bindings, 1);
}

} // namespace sat::lua
