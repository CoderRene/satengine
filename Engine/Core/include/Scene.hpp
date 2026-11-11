#pragma once

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

struct MeshComponent {
    enum class Primitive { Cube };
    Primitive primitive = Primitive::Cube;
    // Empty mesh path selects the built-in cube; texture path empty means white.
    // Both paths are relative to the project root, under Assets/.
    std::string meshPath;
    std::string texturePath;
};

// Paths are relative to the project's Scripts folder. Disabled components do not run.
struct ScriptComponent {
    std::string path;
    bool enabled = true;
};

[[nodiscard]] bool isValidScriptPath(std::string_view path);

// Opaque runtime identity. Zero is invalid. IDs are never reused within a process.
using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidEntityId = 0;

struct Entity {
    [[nodiscard]] EntityId id() const { return m_id; }
    std::string name;
    Transform transform;
    bool hasCamera = false;
    CameraComponent camera;
    bool hasMesh = false;
    MeshComponent mesh;
    std::vector<ScriptComponent> scripts;
    bool hasRigidBody = false;
    RigidBodyComponent rigidBody;
    bool hasCollider = false;
    // Absent colliders do not allocate shape storage. Adding the component
    // initializes ColliderComponent{} (one unit box).
    ColliderComponent collider{.shapes = {}};
    bool hasGravitySource = false;
    GravitySourceComponent gravitySource;

private:
    friend class Scene;
    EntityId m_id = kInvalidEntityId;
};

class Scene {
public:
    Scene();
    PhysicsSettings physicsSettings;

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
    [[nodiscard]] bool destroyEntity(EntityId id);
    [[nodiscard]] bool setMainCameraById(EntityId id);
    // Indices are temporary positions for editor/serialization use, never handles.
    // Creation may invalidate references; removal swaps in the last entity and
    // invalidates references/indices. IDs of surviving entities remain valid.
    [[nodiscard]] Entity* entity(std::size_t index);
    [[nodiscard]] const Entity* entity(std::size_t index) const;
    [[nodiscard]] int mainCameraIndex() const;

    Entity& createEntity(std::string name);
    void addEmpty();
    void clear();
    [[nodiscard]] bool attachScript(std::size_t index, std::string path);
    void addCamera();
    void addCube();
    [[nodiscard]] bool remove(std::size_t index);
    [[nodiscard]] bool rename(std::size_t index, std::string name);
    [[nodiscard]] bool setMainCamera(std::size_t index);

private:
    std::vector<Entity> m_entities;
    std::unordered_map<EntityId, std::size_t> m_entityIndices;
    std::uint64_t m_destructionRevision = 0;
    std::uint64_t m_editRevision = 0;
    int m_cameraCount = 0;
    int m_cubeCount = 0;
    int m_emptyCount = 0;
};

} // namespace sat
