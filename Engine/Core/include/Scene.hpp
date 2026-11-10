#pragma once

#include "Math.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sat {

struct Transform {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

inline Mat4 matrixFromTransform(const Transform& transform) {
    return matrixFromTRS(transform.position, transform.rotation, transform.scale);
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
};

// Paths are relative to the project's Scripts folder. Disabled components do not run.
struct ScriptComponent {
    std::string path;
    bool enabled = true;
};

[[nodiscard]] bool isValidScriptPath(std::string_view path);

struct Entity {
    std::string name;
    Transform transform;
    bool hasCamera = false;
    CameraComponent camera;
    bool hasMesh = false;
    MeshComponent mesh;
    std::vector<ScriptComponent> scripts;
};

class Scene {
public:
    Scene();

    [[nodiscard]] const std::vector<Entity>& entities() const;
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
    static Entity makeCameraEntity(std::string name, bool isMain);

    std::vector<Entity> m_entities;
    int m_cameraCount = 0;
    int m_cubeCount = 0;
    int m_emptyCount = 0;
};

} // namespace sat
