#include "LuaPhysicsBindings.hpp"

#include "LuaBindingUtils.hpp"
#include "PhysicsSystem.hpp"

#include <lua.hpp>

namespace sat::lua {
namespace {
PhysicsSystem &physicsFor(lua_State *state) {
  auto *physics =
      static_cast<PhysicsSystem *>(lua_touserdata(state, lua_upvalueindex(2)));
  if (!physics || !physics->running()) {
    luaL_error(state, "physics is not running");
  }
  return *physics;
}
DVec3 numbers(lua_State *state, int index) {
  return {checkedDouble(state, index), checkedDouble(state, index + 1),
          checkedDouble(state, index + 2)};
}
Vec3 floats(lua_State *state, int index) {
  return {checkedFloat(state, index), checkedFloat(state, index + 1),
          checkedFloat(state, index + 2)};
}
template <class V> int push(lua_State *state, V v) {
  lua_pushnumber(state, v.x);
  lua_pushnumber(state, v.y);
  lua_pushnumber(state, v.z);
  return 3;
}
int checked(lua_State *state, bool success, PhysicsSystem &physics) {
  if (!success) {
    return luaL_error(state, "%s", physics.error().c_str());
  }
  return 0;
}
int velocity(lua_State *state) {
  auto &entity = checkedEntity(state);
  auto &physics = physicsFor(state);
  const auto v = physics.linearVelocity(entity.id());
  if (!v) {
    return luaL_error(state, "entity has no physics body");
  }
  return push(state, *v);
}
int frameId(lua_State *state) {
  const auto id = checkedEntity(state).id();
  auto &physics = physicsFor(state);
  const auto frame = physics.frameFor(id);
  if (!frame) {
    return luaL_error(state, "entity has no physics frame");
  }
  lua_pushinteger(state, static_cast<lua_Integer>(frame->id));
  return 1;
}
int angular(lua_State *state) {
  auto &entity = checkedEntity(state);
  auto &physics = physicsFor(state);
  const auto v = physics.angularVelocity(entity.id());
  if (!v) {
    return luaL_error(state, "entity has no physics body");
  }
  return push(state, *v);
}
int setVelocity(lua_State *state) {
  const auto id = checkedEntity(state).id();
  auto &p = physicsFor(state);
  return checked(state, p.setLinearVelocity(id, numbers(state, 2)), p);
}
template <bool (PhysicsSystem::*operation)(EntityId, Vec3)>
int vectorOperation(lua_State *state) {
  const auto id = checkedEntity(state).id();
  auto &p = physicsFor(state);
  return checked(state, (p.*operation)(id, floats(state, 2)), p);
}
int teleport(lua_State *state) {
  const auto &e = checkedEntity(state);
  auto &p = physicsFor(state);
  Transform t = e.transform;
  t.position = numbers(state, 2);
  t.rotation = floats(state, 5);
  return checked(state, p.teleport(e.id(), t, lua_toboolean(state, 8) != 0), p);
}
int kinematic(lua_State *state) {
  const auto id = checkedEntity(state).id();
  auto &p = physicsFor(state);
  return checked(state,
                 p.moveKinematic(id, numbers(state, 2), floats(state, 5)), p);
}
int gravity(lua_State *state) {
  return push(state, gravityAt(sceneFor(state), numbers(state, 1)));
}
int up(lua_State *state) {
  const auto v = upAt(sceneFor(state), numbers(state, 1));
  if (!v) {
    lua_pushnil(state);
    return 1;
  }
  return push(state, *v);
}
int raycast(lua_State *state) {
  auto &p = physicsFor(state);
  const auto origin = numbers(state, 1);
  const auto direction = floats(state, 4);
  const double distance = checkedDouble(state, 7);
  if (distance <= 0 || std::hypot(direction.x, direction.y, direction.z) == 0) {
    return luaL_error(state,
                      "ray requires positive distance and nonzero direction");
  }
  const lua_Integer ignored = luaL_optinteger(state, 8, 0);
  if (ignored < 0) {
    return luaL_argerror(state, 8, "invalid entity ID");
  }
  const auto hit =
      p.raycast(origin, direction, distance, static_cast<EntityId>(ignored),
                lua_toboolean(state, 9) != 0);
  if (!hit) {
    lua_pushnil(state);
    return 1;
  }
  lua_newtable(state);
  lua_pushinteger(state, static_cast<lua_Integer>(hit->entity));
  lua_setfield(state, -2, "entity");
  lua_pushinteger(state, hit->colliderIndex + 1);
  lua_setfield(state, -2, "colliderIndex");
  lua_pushnumber(state, hit->distance);
  lua_setfield(state, -2, "distance");
  const auto field = [&](const char *name, auto v) {
    lua_newtable(state);
    lua_pushnumber(state, v.x);
    lua_setfield(state, -2, "x");
    lua_pushnumber(state, v.y);
    lua_setfield(state, -2, "y");
    lua_pushnumber(state, v.z);
    lua_setfield(state, -2, "z");
    lua_setfield(state, -2, name);
  };
  field("point", hit->point);
  field("normal", hit->normal);
  return 1;
}
} // namespace
void registerPhysicsBindings(lua_State *state, Scene &scene,
                             PhysicsSystem *physics) {
  lua_newtable(state);
  const luaL_Reg bindings[] = {
      // @lua fun sat.physics.frameId(entity: EntityId): integer
      {"frameId", guarded<frameId>},
      // @lua fun sat.physics.linearVelocity(entity: EntityId): number, number,
      // number
      {"linearVelocity", guarded<velocity>},
      // @lua fun sat.physics.angularVelocity(entity: EntityId): number, number,
      // number
      {"angularVelocity", guarded<angular>},
      // @lua fun sat.physics.setLinearVelocity(entity: EntityId, x: number, y:
      // number, z: number)
      {"setLinearVelocity", guarded<setVelocity>},
      // @lua fun sat.physics.setAngularVelocity(entity: EntityId, x: number, y:
      // number, z: number)
      {"setAngularVelocity",
       guarded<vectorOperation<&PhysicsSystem::setAngularVelocity>>},
      // @lua fun sat.physics.addForce(entity: EntityId, x: number, y: number,
      // z: number)
      {"addForce", guarded<vectorOperation<&PhysicsSystem::addForce>>},
      // @lua fun sat.physics.addTorque(entity: EntityId, x: number, y: number,
      // z: number)
      {"addTorque", guarded<vectorOperation<&PhysicsSystem::addTorque>>},
      // @lua fun sat.physics.addImpulse(entity: EntityId, x: number, y: number,
      // z: number)
      {"addImpulse", guarded<vectorOperation<&PhysicsSystem::addImpulse>>},
      // @lua fun sat.physics.addAngularImpulse(entity: EntityId, x: number, y:
      // number, z: number)
      {"addAngularImpulse",
       guarded<vectorOperation<&PhysicsSystem::addAngularImpulse>>},
      // @lua fun sat.physics.teleport(entity: EntityId, x: number, y: number,
      // z: number, rx: number, ry: number, rz: number, clearVelocity?: boolean)
      {"teleport", guarded<teleport>},
      // @lua fun sat.physics.moveKinematic(entity: EntityId, x: number, y:
      // number, z: number, rx: number, ry: number, rz: number)
      {"moveKinematic", guarded<kinematic>},
      // @lua fun sat.physics.gravityAt(x: number, y: number, z: number):
      // number, number, number
      {"gravityAt", guarded<gravity>},
      // @lua fun sat.physics.upAt(x: number, y: number, z: number): number?,
      // number?, number?
      {"upAt", guarded<up>},
      // @lua fun sat.physics.raycast(x: number, y: number, z: number, dx:
      // number, dy: number, dz: number, distance: number, ignore?: EntityId,
      // includeTriggers?: boolean): table?
      {"raycast", guarded<raycast>},
      {nullptr, nullptr}};
  lua_pushlightuserdata(state, &scene);
  lua_pushlightuserdata(state, physics);
  luaL_setfuncs(state, bindings, 2);
  lua_setfield(state, -2, "physics");
}
} // namespace sat::lua
