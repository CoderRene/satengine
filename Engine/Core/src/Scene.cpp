#include "Scene.hpp"

#include <cctype>

namespace sat {

namespace {

void trimInPlace(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
}

} // namespace

Entity Scene::makeCameraEntity(std::string name, bool isMain) {
    Entity entity;
    entity.name = std::move(name);
    entity.hasCamera = true;
    entity.camera.isMain = isMain;
    if (isMain) {
        entity.transform.position = {0.0f, 1.0f, -5.0f};
    }
    return entity;
}

Scene::Scene() {
    m_entities.push_back(makeCameraEntity("Main Camera", true));
}

const std::vector<Entity>& Scene::entities() const {
    return m_entities;
}

Entity* Scene::entity(std::size_t index) {
    if (index >= m_entities.size()) {
        return nullptr;
    }
    return &m_entities[index];
}

const Entity* Scene::entity(std::size_t index) const {
    if (index >= m_entities.size()) {
        return nullptr;
    }
    return &m_entities[index];
}

int Scene::mainCameraIndex() const {
    for (std::size_t i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i].hasCamera && m_entities[i].camera.isMain) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Entity& Scene::createEntity(std::string name) {
    Entity entity;
    entity.name = std::move(name);
    m_entities.push_back(std::move(entity));
    return m_entities.back();
}

void Scene::addCamera() {
    ++m_cameraCount;
    if (m_cameraCount == 1) {
        m_entities.push_back(makeCameraEntity("Camera", false));
    } else {
        m_entities.push_back(
            makeCameraEntity("Camera " + std::to_string(m_cameraCount), false));
    }
}

void Scene::addCube() {
    ++m_cubeCount;
    std::string name = (m_cubeCount == 1) ? "Cube" : ("Cube " + std::to_string(m_cubeCount));
    Entity& entity = createEntity(std::move(name));
    entity.hasMesh = true;
    entity.mesh.primitive = MeshComponent::Primitive::Cube;
}

bool Scene::remove(std::size_t index) {
    if (index >= m_entities.size()) {
        return false;
    }
    m_entities.erase(m_entities.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool Scene::rename(std::size_t index, std::string name) {
    if (index >= m_entities.size()) {
        return false;
    }
    trimInPlace(name);
    if (name.empty()) {
        return false;
    }
    m_entities[index].name = std::move(name);
    return true;
}

bool Scene::setMainCamera(std::size_t index) {
    if (index >= m_entities.size() || !m_entities[index].hasCamera) {
        return false;
    }
    for (Entity& entity : m_entities) {
        if (entity.hasCamera) {
            entity.camera.isMain = false;
        }
    }
    m_entities[index].camera.isMain = true;
    return true;
}

} // namespace sat
