# Space physics example

Open this folder as a project, select the compound crate in the hierarchy, use
**Focus (F)**, enable **Show Colliders**, and press Play. The scene
is located near (1e12, 1e12, 1e12) meters. A compound crate falls under spherical
gravity onto a flat local ground collider, while a trigger reports overlaps. The
crate script applies an impulse, casts downward rays and logs frame changes.

A traveler starts 2.5 km away and moves toward the scene at 100 m/s with gravity
disabled. It begins in a separate local world and logs its transfer when the
worlds merge. **Scene > Physics Settings** shows the active world count during
Play. Stop restores the original scene. See `docs/Physics.md` for API details and
supported operating limits.

Meshes are intentionally simple cubes; collider outlines show the actual compound
and ground dimensions. The example uses authored gravity, not N-body orbits.
