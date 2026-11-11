#include "LuaMathBindings.hpp"

#include "LuaBindingUtils.hpp"

#include "Math.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cmath>

namespace sat::lua {
namespace {
DVec3 checkedVector(lua_State* state, int first) {
    const double x = checkedDouble(state, first);
    const double y = checkedDouble(state, first + 1);
    const double z = checkedDouble(state, first + 2);
    return {x, y, z};
}

int pushNumber(lua_State* state, double value) {
    if (!std::isfinite(value)) {
        return luaL_error(state, "math result is not finite");
    }
    lua_pushnumber(state, value);
    return 1;
}

int pushVector(lua_State* state, const DVec3& value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return luaL_error(state, "vector result is not finite");
    }
    lua_pushnumber(state, value.x);
    lua_pushnumber(state, value.y);
    lua_pushnumber(state, value.z);
    return 3;
}

double wrapDegrees(double angle) {
    const double wrapped = std::fmod(angle, 360.0);
    const double positive = wrapped < 0.0 ? wrapped + 360.0 : wrapped;
    return positive >= 360.0 ? 0.0 : positive;
}

double shortestAngle(double from, double to) {
    double delta = wrapDegrees(to) - wrapDegrees(from);
    if (delta > 180.0) {
        delta -= 360.0;
    } else if (delta <= -180.0) {
        delta += 360.0;
    }
    return delta;
}

int clamp(lua_State* state) {
    const double value = checkedDouble(state, 1);
    const double minimum = checkedDouble(state, 2);
    const double maximum = checkedDouble(state, 3);
    if (minimum > maximum) {
        return luaL_argerror(state, 3, "maximum must be at least minimum");
    }
    return pushNumber(state, std::clamp(value, minimum, maximum));
}

int lerp(lua_State* state) {
    const double from = checkedDouble(state, 1);
    const double to = checkedDouble(state, 2);
    const double t = std::clamp(checkedDouble(state, 3), 0.0, 1.0);
    return pushNumber(state, std::lerp(from, to, t));
}

int moveTowards(lua_State* state) {
    const double current = checkedDouble(state, 1);
    const double target = checkedDouble(state, 2);
    const double maxDelta = checkedDouble(state, 3);
    if (maxDelta < 0.0) {
        return luaL_argerror(state, 3, "maxDelta must be nonnegative");
    }
    // Clamp before returning, including when the addition overflows toward target.
    const double next = current < target ? std::min(current + maxDelta, target)
                                         : std::max(current - maxDelta, target);
    return pushNumber(state, next);
}

int deltaAngle(lua_State* state) {
    const double from = checkedDouble(state, 1);
    const double to = checkedDouble(state, 2);
    return pushNumber(state, shortestAngle(from, to));
}

int lerpAngle(lua_State* state) {
    const double from = checkedDouble(state, 1);
    const double to = checkedDouble(state, 2);
    const double t = std::clamp(checkedDouble(state, 3), 0.0, 1.0);
    return pushNumber(state, wrapDegrees(wrapDegrees(from) + shortestAngle(from, to) * t));
}

int length(lua_State* state) {
    const DVec3 v = checkedVector(state, 1);
    return pushNumber(state, std::hypot(v.x, v.y, v.z));
}

int distance(lua_State* state) {
    const DVec3 a = checkedVector(state, 1);
    const DVec3 b = checkedVector(state, 4);
    return pushNumber(state, std::hypot(b.x - a.x, b.y - a.y, b.z - a.z));
}

int dot(lua_State* state) {
    const DVec3 a = checkedVector(state, 1);
    const DVec3 b = checkedVector(state, 4);
    return pushNumber(state, a.x * b.x + a.y * b.y + a.z * b.z);
}

int cross(lua_State* state) {
    const DVec3 a = checkedVector(state, 1);
    const DVec3 b = checkedVector(state, 4);
    return pushVector(state, {a.y * b.z - a.z * b.y,
                              a.z * b.x - a.x * b.z,
                              a.x * b.y - a.y * b.x});
}

int normalize(lua_State* state) {
    const DVec3 v = checkedVector(state, 1);
    const double scale = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
    if (scale == 0.0) {
        return pushVector(state, {});
    }
    // Scaling avoids overflow/underflow, including for subnormal input vectors.
    const DVec3 scaled{v.x / scale, v.y / scale, v.z / scale};
    const double inverseLength = 1.0 / std::sqrt(
        scaled.x * scaled.x + scaled.y * scaled.y + scaled.z * scaled.z);
    return pushVector(state, scaled * inverseLength);
}

Mat4 checkedRotation(lua_State* state, int first) {
    const float pitch = checkedFloat(state, first);
    const float yaw = checkedFloat(state, first + 1);
    const float roll = checkedFloat(state, first + 2);
    return rotationMatrixEuler({pitch, yaw, roll});
}

int rotateVector(lua_State* state) {
    const DVec3 v = checkedVector(state, 1);
    const Mat4 rotation = checkedRotation(state, 4);
    return pushVector(state, {
        rotation.m[0] * v.x + rotation.m[4] * v.y + rotation.m[8] * v.z,
        rotation.m[1] * v.x + rotation.m[5] * v.y + rotation.m[9] * v.z,
        rotation.m[2] * v.x + rotation.m[6] * v.y + rotation.m[10] * v.z});
}

int forward(lua_State* state) {
    const Mat4 rotation = checkedRotation(state, 1);
    return pushVector(state, {rotation.m[8], rotation.m[9], rotation.m[10]});
}

int cameraForward(lua_State* state) {
    const float pitch = checkedFloat(state, 1);
    const float yaw = checkedFloat(state, 2);
    const Vec3 direction = sat::cameraForward(pitch, yaw);
    return pushVector(state, {direction.x, direction.y, direction.z});
}
} // namespace

void registerMathBindings(lua_State* state) {
    lua_newtable(state);
    const luaL_Reg bindings[] = {
        // @lua fun sat.math.clamp(value: number, minimum: number, maximum: number): number
        {"clamp", clamp},
        // @lua fun sat.math.lerp(from: number, to: number, t: number): number
        {"lerp", lerp},
        // @lua fun sat.math.moveTowards(current: number, target: number, maxDelta: number): number
        {"moveTowards", moveTowards},
        // @lua fun sat.math.deltaAngle(from: number, to: number): number
        {"deltaAngle", deltaAngle},
        // @lua fun sat.math.lerpAngle(from: number, to: number, t: number): number
        {"lerpAngle", lerpAngle},
        // @lua fun sat.math.length(x: number, y: number, z: number): number
        {"length", length},
        // @lua fun sat.math.distance(ax: number, ay: number, az: number, bx: number, by: number, bz: number): number
        {"distance", distance},
        // @lua fun sat.math.dot(ax: number, ay: number, az: number, bx: number, by: number, bz: number): number
        {"dot", dot},
        // @lua fun sat.math.cross(ax: number, ay: number, az: number, bx: number, by: number, bz: number): x: number, y: number, z: number
        {"cross", cross},
        // @lua fun sat.math.normalize(x: number, y: number, z: number): x: number, y: number, z: number
        {"normalize", normalize},
        // @lua fun sat.math.rotateVector(x: number, y: number, z: number, pitch: number, yaw: number, roll: number): x: number, y: number, z: number
        {"rotateVector", rotateVector},
        // @lua fun sat.math.forward(pitch: number, yaw: number, roll: number): x: number, y: number, z: number
        {"forward", forward},
        // @lua fun sat.math.cameraForward(pitch: number, yaw: number): x: number, y: number, z: number
        {"cameraForward", cameraForward},
        {nullptr, nullptr}
    };
    luaL_setfuncs(state, bindings, 0);
    lua_setfield(state, -2, "math");
}
} // namespace sat::lua
