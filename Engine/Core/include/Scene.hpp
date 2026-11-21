#pragma once

#include "AudioTypes.hpp"
#include "Math.hpp"
#include "PhysicsTypes.hpp"
#include "ScriptFields.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sat {

inline constexpr std::size_t kMaxSceneEntities = 100000;

struct Transform {
    DVec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    bool operator==(const Transform&) const = default;

};

// Maps a descendant's world-space TRS through a change to its ancestor.
class TransformDelta {
public:
    TransformDelta(const Transform& before, const Transform& after);
    [[nodiscard]] Transform apply(const Transform& descendant) const;

private:
    DVec3 m_pivot;
    DVec3 m_displacement;
    DVec3 m_scaleRatio{1, 1, 1};
    Mat4 m_offsetBasis;
    Mat4 m_scaledBasis;
    Mat4 m_rotation;
    bool m_rotated;
    bool m_scaled;
};

[[nodiscard]] Vec3 eulerDegreesFromRotation(const Mat4& rotation, const Vec3& referenceDegrees);

inline Mat4 matrixFromTransform(const Transform& transform, DVec3 renderOrigin = {}) {
    return matrixFromTRS(toVec3(transform.position - renderOrigin), transform.rotation, transform.scale);
}

struct CameraComponent {
    float fieldOfView = 60.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float clearColor[4] = {0.12f, 0.16f, 0.24f, 1.0f};
    bool isMain = false;
    bool operator==(const CameraComponent&) const = default;

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
    bool castShadow = false;
    bool operator==(const LightComponent&) const = default;

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
    bool showEnvironmentSky = false;
    bool weightedTransparency = false;
    bool directionalShadows = true;
    int shadowResolution = 2048;
    float shadowDistance = 100.0f;
    int shadowCascadeCount = 4;
    float shadowSplitLambda = 0.75f;
    bool operator==(const LightingSettings&) const = default;

};

struct MeshComponent {
    enum class Primitive { Cube };
    Primitive primitive = Primitive::Cube;
    // Empty paths select the built-in cube and the default material.
    // Both paths are relative to the project root, under Assets/.
    std::string meshPath;
    std::string materialPath;
    bool combineStatic = false;
    std::uint32_t meshNode = 0; // Zero selects the complete asset.
    bool operator==(const MeshComponent&) const = default;

};

// Paths are relative to the project's Scripts folder. Disabled components do not run.
struct ScriptComponent {
    std::string path;
    bool enabled = true;
    ScriptFields fieldOverrides{};

    bool operator==(const ScriptComponent&) const = default;
};

enum class AnimationRotationBlend : std::uint8_t { Slerp, Nlerp };
enum class AnimationCullingMode : std::uint8_t { AlwaysAnimate, Automatic };

struct AnimatorComponent {
    std::string clip;
    std::string controllerPath;
    bool playOnStart = true;
    bool loop = true;
    float speed = 1;
    float crossfadeSeconds = 0.2f;
    AnimationRotationBlend rotationBlend = AnimationRotationBlend::Slerp;
    AnimationCullingMode cullingMode = AnimationCullingMode::Automatic;
    // Decorative runtime meshes only. Local sphere centered on the entity pivot,
    // enclosing the mesh throughout every clip and transition. Zero disables it.
    float occlusionRadius = 0;
    bool operator==(const AnimatorComponent&) const = default;
};

struct GameUiComponent {
    std::string documentPath;
    std::string fontPath;
    bool visible = true;

    bool operator==(const GameUiComponent&) const = default;
};

[[nodiscard]] bool isUiDocumentPath(std::string_view path);
[[nodiscard]] bool isUiFontPath(std::string_view path);
[[nodiscard]] bool isValidScriptPath(std::string_view path);

// Opaque runtime identity. Zero is invalid. IDs are never reused within a process.
using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidEntityId = 0;

struct Entity {
    [[nodiscard]] EntityId id() const { return m_id; }
    [[nodiscard]] EntityId parentId() const { return m_parentId; }
    [[nodiscard]] bool hasChildren() const { return m_childCount != 0; }
    [[nodiscard]] bool isGroup() const { return createdAsGroup || name == "Group"; }
    std::string name;
    bool createdAsGroup = false;
    Transform transform;
    bool hasCamera = false;
    CameraComponent camera;
    bool hasMesh = false;
    MeshComponent mesh;
    bool hasAnimator = false;
    AnimatorComponent animator;
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
    std::optional<GameUiComponent> gameUi;
    bool hasAudioListener = false;

    bool operator==(const Entity&) const = default;

private:
    friend class Scene;
    EntityId m_id = kInvalidEntityId;
    EntityId m_parentId = kInvalidEntityId;
    std::size_t m_childCount = 0;
};

// Access each scene from one thread at a time. Copies preserve entity IDs for
// snapshots. Newly created entities get fresh IDs. Loading assigns new IDs.
class Scene {
public:
    Scene();
    PhysicsSettings physicsSettings;
    AudioSettings audioSettings;
    LightingSettings lightingSettings;

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
    // Returns the topmost ancestor (or the entity itself), or an invalid ID if absent.
    [[nodiscard]] EntityId rootEntityId(EntityId id) const;
    // Returns the highest non-group entity in the ancestry, including the starting entity.
    [[nodiscard]] EntityId selectionRootId(EntityId id) const;
    // Reparenting preserves the entity's world-space transform.
    // Use kInvalidEntityId as the parent to move an entity to the root.
    [[nodiscard]] bool canSetParent(EntityId childId, EntityId parentId) const;
    [[nodiscard]] bool setParent(EntityId childId, EntityId parentId);
    // Place entities next to a sibling, preserving their relative scene order
    // and world-space transforms while adopting the sibling's parent.
    // Invalid IDs and cyclic moves are rejected before editing the scene.
    [[nodiscard]] bool moveEntitiesBeside(std::span<const EntityId> ids, EntityId siblingId, bool after);
    // Move an entity and its descendants by the same world-space offset.
    // Returns false for a missing entity or an unchanged position.
    bool moveEntity(EntityId id, DVec3 position);
    // Set a world-space transform and apply its change once to every descendant.
    bool setTransform(EntityId id, const Transform& transform);
    // Direct children of a removed entity become roots.
    [[nodiscard]] bool destroyEntity(EntityId id);
    [[nodiscard]] bool setMainCameraById(EntityId id);
    // Indices are temporary positions for editor/serialization use, never handles.
    // Creation, removal, and reordering can invalidate references/indices.
    // Removal swaps in the last entity. IDs of surviving entities remain valid.
    [[nodiscard]] Entity* entity(std::size_t index);
    [[nodiscard]] const Entity* entity(std::size_t index) const;
    [[nodiscard]] int mainCameraIndex() const;

    Entity& createEntity(std::string name);
    // Copy one entity with a fresh ID under parentId, keeping its name if name is empty.
    // The copy is never the main camera. Invalid source, parent, or capacity returns an invalid ID.
    [[nodiscard]] EntityId duplicateEntity(EntityId sourceId, EntityId parentId, std::string name = {});
    // Insert a root from a scene copy with its original ID. The ID must be absent.
    void addEntityCopy(const Entity& entity);
    // Restore a complete ordered entity set from editor history. IDs and
    // hierarchy links must describe a valid scene from the same session.
    void restoreEditorEntities(std::vector<Entity> entities);
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
