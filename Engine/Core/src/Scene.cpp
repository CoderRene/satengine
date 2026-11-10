#include "Scene.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sat {

namespace {

EntityId nextEntityId() {
    // Global uniqueness prevents a stale ID from aliasing an entity after clear,
    // scene replacement, or a new Play session. Never wrap into a previous ID.
    static std::atomic<EntityId> next{1};
    EntityId id = next.load(std::memory_order_relaxed);
    for (;;) {
        if (id > static_cast<EntityId>(std::numeric_limits<std::int64_t>::max())) {
            throw std::overflow_error("Entity ID space exhausted");
        }
        if (next.compare_exchange_weak(id, id + 1, std::memory_order_relaxed)) {
            return id;
        }
    }
}

void trimInPlace(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
}

} // namespace

bool isValidScriptPath(std::string_view path) {
    if (path.empty() || path.front() == '/' || !path.ends_with(".lua") ||
        path.find_first_of("\\:\0", 0, 3) != std::string_view::npos) {
        return false;
    }
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
    return true;
}

Scene::Scene() {
    Entity& camera = createEntity("Main Camera");
    camera.hasCamera = true;
    camera.camera.isMain = true;
    camera.transform.position = {0.0, 1.0, -5.0};
}

const std::vector<Entity>& Scene::entities() const {
    return m_entities;
}

std::optional<std::size_t> Scene::entityIndex(EntityId id) const {
    const auto found = m_entityIndices.find(id);
    return found != m_entityIndices.end() ? std::optional{found->second} : std::nullopt;
}

Entity* Scene::entityById(EntityId id) {
    const auto found = m_entityIndices.find(id);
    return found != m_entityIndices.end() ? &m_entities[found->second] : nullptr;
}

const Entity* Scene::entityById(EntityId id) const {
    const auto found = m_entityIndices.find(id);
    return found != m_entityIndices.end() ? &m_entities[found->second] : nullptr;
}

bool Scene::destroyEntity(EntityId id) {
    const auto found = m_entityIndices.find(id);
    return found != m_entityIndices.end() && remove(found->second);
}

bool Scene::setMainCameraById(EntityId id) {
    const auto found = m_entityIndices.find(id);
    return found != m_entityIndices.end() && setMainCamera(found->second);
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
    entity.m_id = nextEntityId();
    const auto inserted = m_entityIndices.emplace(entity.m_id, m_entities.size()).first;
    try {
        m_entities.push_back(std::move(entity));
    } catch (...) {
        m_entityIndices.erase(inserted);
        throw;
    }
    return m_entities.back();
}

void Scene::clear() {
    ++m_destructionRevision;
    m_entities.clear();
    m_entityIndices.clear();
    m_cameraCount = 0;
    m_cubeCount = 0;
    m_emptyCount = 0;
}

bool Scene::attachScript(std::size_t index, std::string path) {
    Entity* target = entity(index);
    if (target == nullptr || !isValidScriptPath(path)) {
        return false;
    }
    if (std::any_of(target->scripts.begin(), target->scripts.end(), [&](const ScriptComponent& script) {
            return script.path == path;
        })) {
        return false;
    }
    target->scripts.push_back({std::move(path), true});
    return true;
}

void Scene::addEmpty() {
    std::string name;
    do {
        ++m_emptyCount;
        name = m_emptyCount == 1 ? "Empty Entity" : "Empty Entity " + std::to_string(m_emptyCount);
    } while (std::any_of(m_entities.begin(), m_entities.end(), [&](const Entity& item) { return item.name == name; }));
    createEntity(std::move(name));
}

void Scene::addCamera() {
    ++m_cameraCount;
    Entity& camera = createEntity(m_cameraCount == 1 ? "Camera" : "Camera " + std::to_string(m_cameraCount));
    camera.hasCamera = true;
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
    ++m_destructionRevision;
    m_entityIndices.erase(m_entities[index].id());
    if (index + 1 != m_entities.size()) {
        m_entities[index] = std::move(m_entities.back());
        m_entityIndices.find(m_entities[index].id())->second = index;
    }
    m_entities.pop_back();
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
