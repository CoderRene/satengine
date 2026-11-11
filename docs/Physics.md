# Physics and collision

SatEngine uses Jolt 5.6.0 for local rigid-body dynamics and collisions. The engine
keeps universe positions and velocities in doubles and gives each local Jolt
world a translating reference frame. A large common velocity is subtracted before
simulation. Frames are inertial between transfers; their acceleration is zero.
A transfer changes coordinates while preserving universe state.

## Authoring

Add **Compound Collider**, **Rigid Body**, or **Gravity Source** in the Inspector.
A collider without a rigid body is static. A rigid body without a collider is
inactive. Bodies can be Static, Dynamic, or Kinematic. Add up to 64 boxes,
spheres, or Y-axis capsules, with local offsets and Euler rotations. Each compound
is entirely solid or entirely a trigger. Collider dimensions are independent of
mesh geometry; entity scale must be positive and uniform.

Mass is kilograms; position is meters; velocity is meters/second. Transform angles
are Euler degrees (`Rz * Ry * Rx`); angular velocity is radians/second. Damping
defaults to zero so unforced space motion persists. Axis locks refer to universe
XYZ axes because frames do not rotate. Initial velocities are saved with the
component; simulated velocities are runtime state.

**Scene > Physics Settings** controls the timestep, catch-up limit, interaction
and rebase distances, relative-speed limit, and capacities. Settings are frozen
for a Play session. **Show Colliders** in the scene viewport displays depth-tested
outlines; selected shapes are orange, triggers blue, and solids green. Physics
runs only during Play and in the standalone player. Stop restores authored data.

## Gravity and up

There is no implicit global downward gravity. Spherical sources use `mu` (m³/s²),
with acceleration `mu * delta / max(distance, minimumRadius)^3`. Inside the minimum
radius this is a continuous linear field; acceleration is zero at its center.
Uniform sources rotate their acceleration vector with the source transform and
can have a spherical influence radius (zero means global). Fields add together.
Sources can be moved through the usual script transform setters.

`sat.physics.gravityAt(x,y,z)` returns three acceleration components.
`sat.physics.upAt(x,y,z)` returns the opposite normalized direction, or nil below
1e-8 m/s². Gravity accelerates bodies but does not orient them. Gameplay must
choose its camera/body orientation. Game cameras support roll and polar views;
editor fly-camera navigation retains its conventional world-Y up. Select an entity
in the hierarchy and use **Focus (F)** to move the editor camera to large coordinates.

## Scripts and fixed ticks

`onUpdate(dt)` still runs once per frame. Optional `onFixedUpdate(dt)` runs before
each physics tick. Forces and torques accumulate until one tick executes and are
then consumed. Apply continuous forces in `onFixedUpdate`. Time scale is sampled
once per frame; zero pauses ticks. At most eight ticks run by default, and excess
simulation debt is discarded for all worlds together. This is a realtime gameplay
clock, not an astronomical ephemeris clock.

`sat.physics` provides `linearVelocity`, `angularVelocity`, `setLinearVelocity`,
`setAngularVelocity`, `addForce`, `addTorque`, `addImpulse`, `addAngularImpulse`,
`teleport`, `moveKinematic`, `raycast`, `gravityAt`, `upAt`, and `frameId`. Entity
operations take an entity ID first; vector arguments/results are x,y,z numbers.
See generated `sat.lua` for exact signatures. No Jolt handles are exposed.

Velocity inputs are universe-space. `teleport(entity,x,y,z,rx,ry,rz,clearVelocity)`
preserves velocity unless its final argument is true. Existing `sat.setPosition`
and `sat.setRotation` teleport associated bodies immediately. Collider scaling at
runtime is rejected. `moveKinematic` reaches its universe pose over the next tick,
then stops; send a new target each tick for continuous movement.

`raycast(x,y,z,dx,dy,dz,distance,ignore,includeTriggers)` normalizes direction and
searches local worlds, returning nil or `{entity, colliderIndex, point, normal,
distance}`. `colliderIndex` is one-based; point/normal have x,y,z fields. Ray
origins/results are universe-space. Triggers are excluded unless requested.

Optional `onCollisionEnter/Stay/Exit(other)` and `onTriggerEnter/Stay/Exit(other)`
run after physics, one event per entity pair, aggregated across compound shapes.
Callbacks run on the engine thread, never on Jolt workers. Stay continues while
bodies sleep. Exit may name a destroyed entity: check `sat.isEntityValid(other)`.
Frame transfers do not generate artificial exit/enter pairs. Destroyed owners
receive no further updates or contact callbacks. Stop clears pending events.

## Local frames and limits

Default interaction distance is 2 km, rebase distance 1 km, and supported relative
speed 1 km/s. Swept proximity groups merge before stepping. Established neighbors
use twice the proximity margin; splits wait one second to prevent churn. Compact
unchanged groups reuse worlds and shapes. Frames with static bodies use zero
universe velocity; free groups use a mass-weighted common velocity. Frames recenter
when needed; large changes of common velocity rebuild the affected frame.

Local position extent is limited to eight times the interaction distance. Bodies
and collision/contact queues have explicit capacities, and unsupported conditions
stop Play with an error. A transfer may wake a previously sleeping body when its
new relative velocity requires motion. Sleep is restored when the transferred body
can remain at rest in its new frame.

Render positions are camera-relative before float conversion. Editor/game views
use independent double render origins, including picking and gizmos. The scene
grid remains the universe Y=0 plane. This fixes nearby precision, not distant
planet rendering or depth-buffer range.

This implementation does not supply N-body orbital integration, rotating frames,
relativity, terrain streaming/mesh colliders, joints, character controllers,
runtime collider editing, or cross-platform deterministic networking. Use streamed
local geometry for planetary surfaces: planet-sized Jolt colliders are rejected.
Double coordinates still have a finite precision budget; galaxy-wide millimeter
precision is not guaranteed.

## Validation

`PhysicsTests` exercises gravity, serialization, collisions, sleeping, triggers,
compound ray hits, transfers, kinematics, large coordinates and a 1,000-body timing
scenario. `PhysicsScriptingTests` covers fixed callbacks, time scale, contacts,
queries, invalid operations and cleanup. Portable builds disable both the editor
and game player. Windows runtime rendering must additionally be validated on a
D3D11 host.
