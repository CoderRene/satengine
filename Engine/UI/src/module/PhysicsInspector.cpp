#include "PhysicsInspector.hpp"
#include "CursorWrap.hpp"

#include "Scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sat {
namespace {
bool number(const char *label, double &value, double minimum = 0.0) {
  if (!ImGui::InputDouble(label, &value)) {
    return false;
  }
  value = std::max(minimum, value);
  return true;
}
bool velocity(const char *label, DVec3 &v) {
  double values[3] = {v.x, v.y, v.z};
  if (!ImGui::InputScalarN(label, ImGuiDataType_Double, values, 3)) {
    return false;
  }
  v = {values[0], values[1], values[2]};
  return true;
}
} // namespace
bool drawPhysicsInspector(Entity &e) {
  bool changed = false;
  if (e.hasRigidBody &&
      ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &b = e.rigidBody;
    int motion = static_cast<int>(b.motion);
    if (ImGui::Combo("Motion", &motion, "Static\0Dynamic\0Kinematic\0")) {
      b.motion = static_cast<BodyMotion>(motion);
      changed = true;
    }
    changed |= dragFloat("Mass (kg)", &b.mass, 0.1f, 0.001f, 1e12f,
                                "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= dragFloat("Gravity factor", &b.gravityFactor, 0.01f, 0,
                                100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= dragFloat("Linear damping", &b.linearDamping, 0.01f, 0,
                                10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= dragFloat("Angular damping", &b.angularDamping, 0.01f, 0,
                                10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::Checkbox("Allow sleeping", &b.allowSleeping);
    changed |= ImGui::Checkbox("Continuous collision detection", &b.continuous);
    changed |= velocity("Initial universe velocity (m/s)", b.linearVelocity);
    changed |= ImGui::InputFloat3("Initial angular velocity (rad/s)",
                                  &b.angularVelocity.x);
    const char *labels[] = {"Lock translation X", "Lock translation Y",
                            "Lock translation Z", "Lock rotation X",
                            "Lock rotation Y",    "Lock rotation Z"};
    for (unsigned i = 0; i < 6; ++i) {
      const auto bit = static_cast<std::uint8_t>(1u << i);
      bool locked = (b.allowedDofs & bit) == 0;
      if (ImGui::Checkbox(labels[i], &locked)) {
        b.allowedDofs = static_cast<std::uint8_t>(
            locked ? (b.allowedDofs & ~bit) : (b.allowedDofs | bit));
        changed = true;
      }
    }
    if (!e.hasCollider) {
      ImGui::TextWrapped("Inactive: add a collider to create a physics body.");
    }
    if (ImGui::Button("Remove Rigid Body")) {
      e.hasRigidBody = false;
      changed = true;
    }
  }
  if (e.hasCollider &&
      ImGui::CollapsingHeader("Compound Collider",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &c = e.collider;
    changed |= ImGui::Checkbox("Trigger (all shapes)", &c.trigger);
    changed |= dragFloat("Friction", &c.friction, 0.01f, 0, 10, "%.3f",
                                ImGuiSliderFlags_AlwaysClamp);
    changed |= dragFloat("Restitution", &c.restitution, 0.01f, 0, 1,
                                "%.3f", ImGuiSliderFlags_AlwaysClamp);
    if (e.transform.scale.x <= 0 ||
        e.transform.scale.x != e.transform.scale.y ||
        e.transform.scale.x != e.transform.scale.z) {
      ImGui::TextWrapped("Physics requires positive uniform entity scale.");
    }
    int remove = -1, move = -1;
    for (std::size_t i = 0; i < c.shapes.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      auto &s = c.shapes[i];
      if (ImGui::TreeNode("Shape", "Shape %d", static_cast<int>(i + 1))) {
        int type = static_cast<int>(s.shape);
        if (ImGui::Combo("Primitive", &type, "Box\0Sphere\0Capsule\0")) {
          s.shape = static_cast<ColliderShape>(type);
          changed = true;
        }
        changed |= ImGui::InputFloat3("Offset (m)", &s.offset.x);
        changed |= ImGui::InputFloat3("Rotation (degrees)", &s.rotation.x);
        if (s.shape == ColliderShape::Box) {
          changed |= dragFloat3("Half extents (m)", &s.halfExtents.x,
                                       0.01f, 0.001f, 1e6f, "%.3f",
                                       ImGuiSliderFlags_AlwaysClamp);
        } else {
          changed |=
              dragFloat("Radius (m)", &s.radius, 0.01f, 0.001f, 1e6f,
                               "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
        if (s.shape == ColliderShape::Capsule) {
          changed |= dragFloat("Cylinder half-height (m)", &s.halfHeight,
                                      0.01f, 0.001f, 1e6f, "%.3f",
                                      ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::BeginDisabled(c.shapes.size() == 1);
        if (ImGui::Button("Remove Shape")) {
          remove = static_cast<int>(i);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::Button("Move Up")) {
          move = static_cast<int>(i);
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    if (remove >= 0) {
      c.shapes.erase(c.shapes.begin() + remove);
      changed = true;
    }
    if (move > 0) {
      std::swap(c.shapes[static_cast<std::size_t>(move)],
                c.shapes[static_cast<std::size_t>(move - 1)]);
      changed = true;
    }
    ImGui::BeginDisabled(c.shapes.size() >= 64);
    if (ImGui::Button("Add Shape")) {
      c.shapes.emplace_back();
      changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Remove Collider")) {
      e.hasCollider = false;
      changed = true;
    }
  }
  if (e.hasGravitySource &&
      ImGui::CollapsingHeader("Gravity Source",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &g = e.gravitySource;
    int mode = static_cast<int>(g.mode);
    if (ImGui::Combo("Field", &mode, "Spherical\0Uniform\0")) {
      g.mode = static_cast<GravityMode>(mode);
      changed = true;
    }
    if (g.mode == GravityMode::Spherical) {
      changed |= number("Mu (m^3/s^2)", g.mu);
      changed |= number("Minimum radius (m)", g.minimumRadius, 0.001);
    } else {
      changed |=
          ImGui::InputFloat3("Local acceleration (m/s^2)", &g.acceleration.x);
      changed |= number("Influence radius (0 = global)", g.influenceRadius);
    }
    if (ImGui::Button("Remove Gravity Source")) {
      e.hasGravitySource = false;
      changed = true;
    }
  }
  return changed;
}
bool drawPhysicsSettings(PhysicsSettings &s) {
  bool changed = false;
  changed |= number("Fixed timestep (seconds)", s.fixedSeconds, 0.0001);
  s.fixedSeconds = std::min(s.fixedSeconds, 1.0);
  int steps = static_cast<int>(s.maxCatchUpSteps);
  if (ImGui::InputInt("Maximum catch-up steps", &steps)) {
    s.maxCatchUpSteps = static_cast<std::uint32_t>(std::clamp(steps, 1, 64));
    changed = true;
  }
  changed |= number("Interaction distance (m)", s.interactionDistance, 1);
  changed |= number("Rebase distance (m)", s.rebaseDistance, 1);
  if (number("Maximum relative speed (m/s)", s.maxRelativeSpeed, 1)) {
    s.maxRelativeSpeed = std::min(s.maxRelativeSpeed, kMaxPhysicsRelativeSpeed);
    changed = true;
  }
  int bodies = static_cast<int>(s.maxBodies),
      events = static_cast<int>(s.maxContactEvents);
  if (ImGui::InputInt("Body capacity", &bodies)) {
    s.maxBodies = static_cast<std::uint32_t>(std::clamp(bodies, 1, 100000));
    changed = true;
  }
  if (ImGui::InputInt("Contact capacity", &events)) {
    s.maxContactEvents =
        static_cast<std::uint32_t>(std::clamp(events, 1, 1048576));
    changed = true;
  }
  ImGui::TextWrapped(
      "Changes apply on the next Play session. Frames translate only; rotating "
      "frames and N-body gravity are not enabled.");
  return changed;
}
} // namespace sat
