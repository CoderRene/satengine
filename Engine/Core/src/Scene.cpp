#include "Scene.hpp"

#include "AssetData.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <limits>
#include <numbers>
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

Vec3 closestDegrees(const Vec3& value, const Vec3& reference) {
    const auto closest = [](double angle, double target) {
        return static_cast<float>(angle + 360.0 * std::round((target - angle) / 360.0));
    };
    return {closest(value.x, reference.x), closest(value.y, reference.y), closest(value.z, reference.z)};
}

double eulerDistance(const Vec3& a, const Vec3& b) {
    const double x = static_cast<double>(a.x) - b.x;
    const double y = static_cast<double>(a.y) - b.y;
    const double z = static_cast<double>(a.z) - b.z;
    return x * x + y * y + z * z;
}

DVec3 transformOffset(const Mat4& basis, const DVec3& offset) {
    const auto& m = basis.m;
    return {m[0] * offset.x + m[4] * offset.y + m[8] * offset.z,
        m[1] * offset.x + m[5] * offset.y + m[9] * offset.z,
        m[2] * offset.x + m[6] * offset.y + m[10] * offset.z};
}

} // namespace

Vec3 eulerDegreesFromRotation(const Mat4& rotation, const Vec3& referenceDegrees) {
    const double cy = std::hypot(static_cast<double>(rotation.m[0]), static_cast<double>(rotation.m[1]));
    const double yaw = std::atan2(-static_cast<double>(rotation.m[2]), cy);
    constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi_v<double>;
    if (cy < 0.000001) {
        const double roll = referenceDegrees.z / kRadiansToDegrees;
        const double pitch = yaw > 0 ? std::atan2(rotation.m[4], rotation.m[5]) + roll :
            std::atan2(-rotation.m[4], rotation.m[5]) - roll;
        return closestDegrees({static_cast<float>(pitch * kRadiansToDegrees), yaw > 0 ? 90.0f : -90.0f, referenceDegrees.z}, referenceDegrees);
    }
    const Vec3 first{static_cast<float>(std::atan2(rotation.m[6], rotation.m[10]) * kRadiansToDegrees),
        static_cast<float>(yaw * kRadiansToDegrees), static_cast<float>(std::atan2(rotation.m[1], rotation.m[0]) * kRadiansToDegrees)};
    const auto a = closestDegrees(first, referenceDegrees);
    const auto b = closestDegrees({first.x + 180, 180 - first.y, first.z + 180}, referenceDegrees);
    return eulerDistance(a, referenceDegrees) <= eulerDistance(b, referenceDegrees) ? a : b;
}

TransformDelta::TransformDelta(const Transform& before, const Transform& after)
    : m_pivot(before.position), m_displacement(after.position - before.position),
      m_rotated(before.rotation != after.rotation), m_scaled(before.scale != after.scale) {
    if (!m_rotated && !m_scaled) {
        return;
    }
    const Mat4 oldBasis = rotationMatrixEuler(before.rotation);
    Mat4 inverseBasis = oldBasis;
    std::swap(inverseBasis.m[1], inverseBasis.m[4]);
    std::swap(inverseBasis.m[2], inverseBasis.m[8]);
    std::swap(inverseBasis.m[6], inverseBasis.m[9]);
    const Mat4 newBasis = m_rotated ? rotationMatrixEuler(after.rotation) : oldBasis;
    if (m_rotated) {
        m_rotation = newBasis * inverseBasis;
    }
    if (m_scaled) {
        // A collapsed axis has no inverse. Preserve what remains on that axis.
        const auto ratio = [](double oldScale, double newScale) {
            return oldScale == 0 ? 1.0 : newScale / oldScale;
        };
        m_scaleRatio = {ratio(before.scale.x, after.scale.x), ratio(before.scale.y, after.scale.y),
            ratio(before.scale.z, after.scale.z)};
        m_offsetBasis = inverseBasis;
        m_scaledBasis = newBasis;
    }
}

Transform TransformDelta::apply(const Transform& descendant) const {
    Transform result = descendant;
    if (m_rotated || m_scaled) {
        const auto offset = descendant.position - m_pivot;
        if (m_scaled) {
            const auto local = transformOffset(m_offsetBasis, offset);
            result.position = m_pivot + transformOffset(m_scaledBasis,
                {local.x * m_scaleRatio.x, local.y * m_scaleRatio.y, local.z * m_scaleRatio.z});
            // World-space TRS stores axis scales and has no shear component.
            result.scale = {static_cast<float>(descendant.scale.x * m_scaleRatio.x),
                static_cast<float>(descendant.scale.y * m_scaleRatio.y),
                static_cast<float>(descendant.scale.z * m_scaleRatio.z)};
        } else {
            result.position = m_pivot + transformOffset(m_rotation, offset);
        }
        if (m_rotated) {
            result.rotation = eulerDegreesFromRotation(m_rotation * rotationMatrixEuler(descendant.rotation), descendant.rotation);
        }
    }
    result.position = result.position + m_displacement;
    return result;
}

bool isUiDocumentPath(std::string_view path) {
    return path.starts_with("Assets/UI/") && path.ends_with(".rml") && isValidAssetPath(path);
}

bool isUiFontPath(std::string_view path) {
    return path.starts_with("Assets/UI/") && (path.ends_with(".ttf") || path.ends_with(".otf")) &&
        isValidAssetPath(path);
}

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

EntityId Scene::rootEntityId(EntityId id) const {
    const Entity* entity = entityById(id);
    if (!entity) {
        return kInvalidEntityId;
    }
    while (entity->parentId() != kInvalidEntityId) {
        entity = entityById(entity->parentId());
    }
    return entity->id();
}

bool Scene::canSetParent(const Entity& child, const Entity* parent) const {
    if (parent == &child) {
        return false;
    }
    if (child.m_childCount != 0) {
        for (const Entity* ancestor = parent; ancestor; ancestor = entityById(ancestor->parentId())) {
            if (ancestor == &child) {
                return false;
            }
        }
    }
    return true;
}

bool Scene::canSetParent(EntityId childId, EntityId parentId) const {
    const Entity* child = entityById(childId);
    const Entity* parent = entityById(parentId);
    return child && (parent || parentId == kInvalidEntityId) && canSetParent(*child, parent);
}

bool Scene::setParent(EntityId childId, EntityId parentId) {
    Entity* child = entityById(childId);
    Entity* parent = entityById(parentId);
    if (!child || (!parent && parentId != kInvalidEntityId) || !canSetParent(*child, parent)) {
        return false;
    }
    if (child->m_parentId == parentId) {
        return true;
    }
    if (Entity* previous = entityById(child->m_parentId)) {
        --previous->m_childCount;
    }
    child->m_parentId = parentId;
    if (parent) {
        ++parent->m_childCount;
    }
    markEdited();
    return true;
}

bool Scene::moveEntitiesBeside(std::span<const EntityId> ids, EntityId siblingId, bool after) {
    const auto siblingIndex = entityIndex(siblingId);
    if (ids.empty() || !siblingIndex) {
        return false;
    }
    const EntityId parentId = m_entities[*siblingIndex].parentId();
    const Entity* parent = entityById(parentId);
    std::vector<std::size_t> moved;
    moved.reserve(ids.size());
    for (const EntityId id : ids) {
        const auto index = entityIndex(id);
        if (!index || *index == *siblingIndex || !canSetParent(m_entities[*index], parent)) {
            return false;
        }
        moved.push_back(*index);
    }
    std::sort(moved.begin(), moved.end());
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());

    const std::size_t first = std::min(moved.front(), *siblingIndex);
    const std::size_t last = std::max(moved.back(), *siblingIndex);
    std::vector<EntityId> order;
    order.reserve(last - first + 1);
    const auto appendMoved = [&] {
        for (const std::size_t index : moved) {
            order.push_back(m_entities[index].id());
        }
    };
    auto nextMoved = moved.begin();
    for (std::size_t index = first; index <= last; ++index) {
        if (nextMoved != moved.end() && index == *nextMoved) {
            ++nextMoved;
            continue;
        }
        if (index == *siblingIndex && !after) {
            appendMoved();
        }
        order.push_back(m_entities[index].id());
        if (index == *siblingIndex && after) {
            appendMoved();
        }
    }

    for (const std::size_t index : moved) {
        (void)setParent(m_entities[index].id(), parentId);
    }
    bool orderChanged = false;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const std::size_t index = first + i;
        Entity& current = m_entities[index];
        if (current.id() != order[i]) {
            auto destination = m_entityIndices.find(order[i]);
            auto displaced = m_entityIndices.find(current.id());
            std::swap(current, m_entities[destination->second]);
            std::swap(destination->second, displaced->second);
            orderChanged = true;
        }
    }
    if (orderChanged) {
        markEdited();
    }
    return true;
}

bool Scene::moveEntity(EntityId id, DVec3 position) {
    const Entity* root = entityById(id);
    if (!root) {
        return false;
    }
    Transform next = root->transform;
    next.position = position;
    return setTransform(id, next);
}

bool Scene::setTransform(EntityId id, const Transform& transform) {
    Entity* root = entityById(id);
    if (!root || root->transform == transform) {
        return false;
    }
    const Transform previous = root->transform;
    root->transform = transform;
    if (root->m_childCount != 0) {
        const TransformDelta delta(previous, transform);
        for (Entity& entity : m_entities) {
            EntityId parentId = entity.parentId();
            while (parentId != kInvalidEntityId) {
                if (parentId == id) {
                    entity.transform = delta.apply(entity.transform);
                    break;
                }
                parentId = entityById(parentId)->parentId();
            }
        }
    }
    markEdited();
    return true;
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
    return insertEntity(std::move(entity));
}

EntityId Scene::duplicateEntity(EntityId sourceId, EntityId parentId, std::string name) {
    const Entity* source = entityById(sourceId);
    const auto parent = entityIndex(parentId);
    if (!source || (parentId != kInvalidEntityId && !parent) || entityCount() >= kMaxSceneEntities) {
        return kInvalidEntityId;
    }

    Entity copy = *source;
    if (!name.empty()) {
        copy.name = std::move(name);
    }
    copy.camera.isMain = false;
    copy.m_id = nextEntityId();
    copy.m_parentId = parentId;
    copy.m_childCount = 0;
    const EntityId id = insertEntity(std::move(copy)).id();
    if (parent) {
        ++m_entities[*parent].m_childCount;
    }
    return id;
}

void Scene::addEntityCopy(const Entity& entity) {
    Entity copy = entity;
    copy.m_parentId = kInvalidEntityId;
    copy.m_childCount = 0;
    (void)insertEntity(std::move(copy));
}

void Scene::restoreEditorEntities(std::vector<Entity> entities) {
    std::unordered_map<EntityId, std::size_t> indices;
    indices.reserve(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index) {
        indices.emplace(entities[index].id(), index);
    }
    m_entities = std::move(entities);
    m_entityIndices = std::move(indices);
    ++m_destructionRevision;
    markEdited();
}

Entity& Scene::insertEntity(Entity entity) {
    const auto [inserted, added] = m_entityIndices.emplace(entity.m_id, m_entities.size());
    if (!added) {
        throw std::logic_error("Entity ID already exists in scene");
    }
    try {
        m_entities.push_back(std::move(entity));
    } catch (...) {
        m_entityIndices.erase(inserted);
        throw;
    }
    markEdited();
    return m_entities.back();
}

void Scene::clear() {
    markEdited();
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
    markEdited();
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

void Scene::addLight() {
    createEntity("Light").hasLight = true;
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
    markEdited();
    ++m_destructionRevision;
    const Entity& removed = m_entities[index];
    if (Entity* parent = entityById(removed.parentId())) {
        --parent->m_childCount;
    }
    if (removed.m_childCount != 0) {
        for (Entity& child : m_entities) {
            if (child.m_parentId == removed.id()) {
                child.m_parentId = kInvalidEntityId;
            }
        }
    }
    m_entityIndices.erase(removed.id());
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
    if (m_entities[index].name != name) {
        m_entities[index].name = std::move(name);
        markEdited();
    }
    return true;
}

bool Scene::setMainCamera(std::size_t index) {
    if (index >= m_entities.size() || !m_entities[index].hasCamera) {
        return false;
    }
    bool changed = false;
    for (std::size_t cameraIndex = 0; cameraIndex < m_entities.size(); ++cameraIndex) {
        Entity& entity = m_entities[cameraIndex];
        if (entity.hasCamera) {
            const bool isMain = cameraIndex == index;
            changed |= entity.camera.isMain != isMain;
            entity.camera.isMain = isMain;
        }
    }
    if (changed) { markEdited(); }
    return true;
}

} // namespace sat
