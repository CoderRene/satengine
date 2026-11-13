# Scene transform tools

Select an entity in Hierarchy or the Scene view, then choose a tool in the Scene
toolbar or use its shortcut while the Scene is focused and the pointer is over
its image.

| Key | Tool | Handles |
| --- | --- | --- |
| W | Move | Red X, green Y, and blue Z arrows |
| R | Rotate | Red X, green Y, and blue Z rotation rings |
| T | Scale | Axis shafts with cube tips; a center cube scales all axes together |

Move starts in **Global** space. Rotate starts in **Local** space. Each remembers
its last space for the editor session. Global axes follow the world; Local axes
follow the selected entity's rotation. Scale always uses Local axes, so its space
selector is disabled. Gizmo orientation ignores the entity's scale and mirrored
axes. Gizmos keep a consistent screen size as camera distance or zoom changes.

Hover a handle to highlight it yellow; dragging highlights it orange. A handle
hit takes priority over selecting an entity behind it. Entity selection retains
its existing bounding-box picking; this feature changes only gizmo picking.

- Move arrows constrain movement to one axis, preserving double-precision world
  positions.
- Rotation rings constrain rotation about one axis. Angles use the engine's
  Euler degrees (`Rz * Ry * Rx`); matrix composition implements Local/Global
  rotation. Crossing 180 degrees or making multiple turns does not wrap the drag.
- Scale handles change one local scale component. Moving outward by one projected
  handle length doubles its magnitude. Dragging the center diagonally upward and
  right scales uniformly; moving the opposite way shrinks it.
- Scaling preserves existing signs and prevents crossing zero. The minimum
  magnitude is `0.0001`; zero components recover positively only after movement
  begins. Negative scale may still be entered directly in Inspector.

Release the left mouse button to commit. **Escape** restores the starting
transform. Changing selection, losing Scene/application focus, hiding or resizing
its viewport, starting or stopping Play, or switching projects cancels an active
drag. If the entity was destroyed, the drag is abandoned. A click without movement does not
change the transform. During Play, **Live Game** shows the running scene from
the editor camera. Gizmo and Inspector transform edits appear immediately in
both the Scene and Game views. During Play the Inspector reads the current
runtime transform and saves the axes you edit. Scripts and physics continue
updating the entity on subsequent frames. Use the prompt after Stop to save or discard
your edits.

Tool shortcuts are suppressed during text editing, with modifier keys, camera
navigation, and active gizmo drags. Hold **right mouse** to look around and use
**WASD** to fly the camera. Middle mouse pans; the wheel moves the camera forward
or backward. Camera navigation is suppressed while dragging a transform handle.

There are no plane-move handles, snapping, multi-selection, shear transforms, or
undo-history integration in this version.

## Native implementation and validation

`SceneDrawList` carries `showTransformGizmo` and a `TransformGizmoInstance` with
its tool, origin, rotation-only basis and highlighted/active handle. The renderer
owns immutable geometry for all three modes and uses the same ring vertices and
screen-size calculation as the portable picking/drag code. Drag ownership uses a
stable entity ID rather than a temporary scene-vector index.

Portable tests cover Local/Global math, gimbal-lock equivalence, full-turn
continuity, matching handle picking, scale limits, cancellation, and entity
lifetime changes. On Windows, `TransformGizmoRenderingTests` uses WARP to draw
all three styles and verify distinct images and hover/active highlight colors.
