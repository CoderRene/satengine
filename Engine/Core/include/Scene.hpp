#pragma once

#include "AudioTypes.hpp"
#include "Math.hpp"
#include "PhysicsTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sat {

struct Transform {
    DVec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

inline Mat4 matrixFromTransform(const Transform& transform, DVec3 renderOrigin = {}) {
    return matrixFromTRS(toVec3(transform.position - renderOrigin), transform.rotation, transform.scale);
}

struct CameraComponent {
    float fieldOfView = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float clearColor[4] = {0.12f, 0.16f, 0.24f, 1.0f};
    bool isMain = false;
};

enum class LightType { Directional, Point, Spot };

// Direction follows the entity rotation's +Z axis. Range is in world units.
// Spot cone angles are half-angles in degrees. Colors and intensity are linear.
struct LightComponent {
    LightType type = LightType::Directional;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 35.0f;
};

struct BloomSettings {
    bool enabled = true;
    float threshold = 1.0f;
    float softKnee = 0.0f;
    float intensity = 0.15f;
    float radius = 6.0f;
    float tint[3] = {1.0f, 1.0f, 1.0f};
    float highlightClamp = 65504.0f;
    int quality = 1;

    bool operator==(const BloomSettings&) const = default;
};

struct LightingSettings {
    BloomSettings bloom;
    float ambientColor[3] = {1.0f, 1.0f, 1.0f};
    float ambientIntensity = 0.3f;
    std::string environmentPath;
    float environmentIntensity = 1.0f;
    bool directionalShadows = true;
    int shadowResolution = 2048;
    float shadowDistance = 100.0f;
    int shadowCascadeCount = 4;
    float shadowSplitLambda = 0.75f;
};

struct MeshComponent {
    enum class Primitive { Cube };
    Primitive primitive = Primitive::Cube;
    // Empty paths select the built-in cube and the default material.
    // Both paths are relative to the project root, under Assets/.
    std::string meshPath;
    std::string materialPath;
};

// Paths are relative to the project's Scripts folder. Disabled components do not run.
struct ScriptComponent {
    std::string path;
    bool enabled = true;

    bool operator==(const ScriptComponent&) const = default;
};

[[nodiscard]] bool isValidScriptPath(std::string_view path);

// Opaque runtime identity. Zero is invalid. IDs are never reused within a process.
using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidEntityId = 0;

struct Entity {
    [[nodiscard]] EntityId id() const { return m_id; }
    [[nodiscard]] EntityId parentId() const { return m_parentId; }
    std::string name;
    Transform transform;
    bool hasCamera = false;
    CameraComponent camera;
    bool hasMesh = false;
    MeshComponent mesh;
    bool hasLight = false;
    LightComponent light;
    std::vector<ScriptComponent> scripts;
    bool hasRigidBody = false;
    RigidBodyComponent rigidBody;
    bool hasCollider = false;
    // Absent colliders do not allocate shape storage. Adding the component
    // initializes ColliderComponent{} (one unit box).
    ColliderComponent collider{.shapes = {}};
    bool hasGravitySource = false;
    GravitySourceComponent gravitySource;
    bool hasAudioSource = false;
    AudioSourceComponent audioSource;
    bool hasAudioListener = false;

private:
    friend class Scene;
    EntityId m_id = kInvalidEntityId;
    EntityId m_parentId = kInvalidEntityId;
    std::size_t m_childCount = 0;
};

class Scene {
public:
    Scene();
    PhysicsSettings physicsSettings;
    AudioSettings audioSettings;
    LightingSettings lightingSettings;

    // Window-thread only. Copies preserve identity for snapshots; newly created
    // entities always get fresh IDs. IDs are not serialized. Load replaces them.
    [[nodiscard]] const std::vector<Entity>& entities() const;
    [[nodiscard]] std::size_t entityCount() const { return m_entities.size(); }
    // Structural mutators advance this automatically. After editing fields through
    // a borrowed Entity*, callers must markEdited(). Reads must not mark edits.
    // Copies preserve the revision, allowing editor Play snapshots to restore it.
    [[nodiscard]] std::uint64_t editRevision() const { return m_editRevision; }
    void markEdited() { ++m_editRevision; }
    // Changes on remove/clear so runtime hosts can retire destroyed owners lazily.
    [[nodiscard]] std::uint64_t destructionRevision() const { return m_destructionRevision; }
    [[nodiscard]] std::optional<std::size_t> entityIndex(EntityId id) const;
    [[nodiscard]] Entity* entityById(EntityId id);
    [[nodiscard]] const Entity* entityById(EntityId id) const;
    // Hierarchy orders entities in the editor. Transforms remain in world space.
    // Use kInvalidEntityId as the parent to move an entity to the root.
    [[nodiscard]] bool canSetParent(EntityId childId, EntityId parentId) const;
    [[nodiscard]] bool setParent(EntityId childId, EntityId parentId);
    // Direct children of a removed entity become roots.
    [[nodiscard]] bool destroyEntity(EntityId id);
    [[nodiscard]] bool setMainCameraById(EntityId id);
    // Indices are temporary positions for editor/serialization use, never handles.
    // Creation may invalidate references; removal swaps in the last entity and
    // invalidates references/indices. IDs of surviving entities remain valid.
    [[nodiscard]] Entity* entity(std::size_t index);
    [[nodiscard]] const Entity* entity(std::size_t index) const;
    [[nodiscard]] int mainCameraIndex() const;

    Entity& createEntity(std::string name);
    // Insert a root from a scene copy with its original ID. The ID must be absent.
    void addEntityCopy(const Entity& entity);
    void addEmpty();
    void clear();
    [[nodiscard]] bool attachScript(std::size_t index, std::string path);
    void addCamera();
    void addCube();
    void addLight();
    [[nodiscard]] bool remove(std::size_t index);
    [[nodiscard]] bool rename(std::size_t index, std::string name);
    [[nodiscard]] bool setMainCamera(std::size_t index);

private:
    Entity& insertEntity(Entity entity);
    [[nodiscard]] bool canSetParent(const Entity& child, const Entity* parent) const;

    std::vector<Entity> m_entities;
    std::unordered_map<EntityId, std::size_t> m_entityIndices;
    std::uint64_t m_destructionRevision = 0;
    std::uint64_t m_editRevision = 0;
    int m_cameraCount = 0;
    int m_cubeCount = 0;
    int m_emptyCount = 0;
};

} // namespace sat
