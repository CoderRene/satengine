#include "AnimationSystem.hpp"

#include "AnimationAsset.hpp"
#include "AnimationGraph.hpp"
#include "AssetImporter.hpp"
#include "module/AnimationSampler.hpp"
#include "module/AnimationStateMachine.hpp"
#include "module/WorkerPool.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sat {
namespace {
bool isPartOfRig(const Entity& entity, std::string_view path) {
    return !entity.hasMesh || (entity.mesh.meshNode != 0 && entity.mesh.meshPath == path);
}

constexpr unsigned kOcclusionInterval = 4;
constexpr std::uint64_t kMaxOcclusionAge = 4;

bool sameTransform(const Transform& a, const Transform& b) {
    return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}

bool matchesCamera(const AnimationOcclusionStamp& stamp, const Entity& camera) {
    return camera.id() == stamp.camera && sameTransform(stamp.cameraTransform, camera.transform) &&
        stamp.fieldOfView == camera.camera.fieldOfView && stamp.nearClip == camera.camera.nearClip &&
        stamp.farClip == camera.camera.farClip;
}

bool isAffine(const Mat4& matrix) {
    return matrix.m[3] == 0 && matrix.m[7] == 0 && matrix.m[11] == 0;
}

Mat4 affineMultiply(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int column = 0; column < 16; column += 4) {
        for (int row = 0; row < 3; ++row) {
            out.m[column + row] = a.m[row] * b.m[column] + a.m[4 + row] * b.m[column + 1] +
                a.m[8 + row] * b.m[column + 2];
        }
    }
    for (int row = 0; row < 3; ++row) { out.m[12 + row] += a.m[12 + row] * b.m[15]; }
    // Inverting a float affine matrix can leave its homogeneous scale close to one.
    out.m[15] = a.m[15] * b.m[15];
    return out;
}

struct AnimationFrustum {
    DVec3 eye;
    Vec3 right, up, forward;
    float verticalSlope, horizontalSlope, verticalRadius, horizontalRadius, nearClip, farClip;
};

AnimationFrustum animationFrustum(const Entity& camera) {
    // The runtime camera does not carry the viewport dimensions. Allow up to 32:9.
    constexpr float kMaxAspect = 32.0f / 9.0f;
    const auto view = viewFromEuler({}, camera.transform.rotation);
    const float vertical = std::tan(camera.camera.fieldOfView * kDegToRad * 0.5f);
    const float horizontal = vertical * kMaxAspect;
    return {camera.transform.position, {view.m[0], view.m[4], view.m[8]}, {view.m[1], view.m[5], view.m[9]},
        {view.m[2], view.m[6], view.m[10]}, vertical, horizontal, std::sqrt(1 + vertical * vertical),
        std::sqrt(1 + horizontal * horizontal), camera.camera.nearClip, camera.camera.farClip};
}

unsigned evaluationInterval(const AnimationFrustum& camera, const Transform& transform, Vec3 localCenter, float localRadius) {
    const Vec3 scaledCenter{localCenter.x * transform.scale.x, localCenter.y * transform.scale.y,
        localCenter.z * transform.scale.z};
    const auto rotatedCenter = transform.rotation == Vec3{} ? scaledCenter :
        transformPoint(rotationMatrixEuler(transform.rotation), scaledCenter);
    const auto center = toVec3(transform.position - camera.eye) + rotatedCenter;
    const float scale = std::max({std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z)});
    const float radius = localRadius * scale * 1.25f + 0.5f;
    const float depth = dot(center, camera.forward);
    if (depth + radius < camera.nearClip || depth - radius > camera.farClip ||
        std::abs(dot(center, camera.right)) > depth * camera.horizontalSlope + radius * camera.horizontalRadius ||
        std::abs(dot(center, camera.up)) > depth * camera.verticalSlope + radius * camera.verticalRadius) { return 0; }
    constexpr float kNearDistance = 20, kMiddleDistance = 50;
    const float distanceSquared = dot(center, center);
    if (distanceSquared < kNearDistance * kNearDistance) { return 1; }
    return distanceSquared < kMiddleDistance * kMiddleDistance ? 2 : 4;
}
}

std::string_view animatorMeshPath(const Scene& scene, const Entity& entity) {
    if (entity.hasMesh) {
        return isGltfAssetPath(entity.mesh.meshPath) ? std::string_view(entity.mesh.meshPath) : std::string_view{};
    }
    std::string_view path;
    for (const auto& mesh : scene.entities()) {
        if (!mesh.hasMesh || mesh.mesh.meshNode == 0) { continue; }
        for (const auto* parent = scene.entityById(mesh.parentId()); parent && isPartOfRig(*parent, mesh.mesh.meshPath);
            parent = scene.entityById(parent->parentId())) {
            if (parent->id() != entity.id()) { continue; }
            if (!path.empty() && path != mesh.mesh.meshPath) { return {}; }
            path = mesh.mesh.meshPath;
            break;
        }
    }
    return isGltfAssetPath(path) ? path : std::string_view{};
}

float animationOcclusionWorldRadius(const Entity& entity) {
    const auto scale = entity.transform.scale;
    return entity.animator.occlusionRadius *
        std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
}

struct AnimationSystem::Impl {
    struct BoundClip {
        const CompiledClip* clip = nullptr;
        float start = 0, end = 0, speed = 1;
    };
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const { return std::hash<std::string_view>{}(value); }
    };
    struct Rig {
        struct Binding {
            CompiledClip clip;
            BoundClip range;
        };
        AnimationData data;
        float scale = 1;
        bool affine = true;
        std::vector<std::size_t> order;
        std::vector<AnimationBounds> jointBounds;
        std::vector<Vec3> nodeOrigins;
        std::unordered_map<std::string, Binding, NameHash, std::equal_to<>> bindings;
    };
    struct CachedGraph {
        AnimationGraph asset;
        AnimationTransitionLists transitions;
    };
    struct Instance {
        EntityId id = kInvalidEntityId;
        std::string meshPath;
        std::uint64_t sceneRevision = 0, poseRevision = 0;
        AnimatorComponent settings;
        Rig* rig = nullptr;
        const AnimationGraph* graph = nullptr;
        const CachedGraph* cachedGraph = nullptr;
        std::unordered_map<std::string, std::size_t, NameHash, std::equal_to<>> parameterIndices;
        std::vector<double> parameters;
        std::unordered_set<std::string> reportedParameters;
        std::vector<AnimationPose> pose, source;
        std::vector<Mat4> worlds;
        AnimationBounds bounds;
        Vec3 boundsCenter;
        float boundsRadius = 0;
        std::vector<std::size_t> meshEntities;
        std::vector<std::uint8_t> animated, sourceAnimated;
        std::vector<std::size_t> cursors;
        const BoundClip* clip = nullptr;
        const BoundClip* sourceClip = nullptr;
        double sourceTime = 0;
        float sourceSpeed = 1;
        bool sourceLoop = true, frozenSource = false;
        std::vector<std::size_t> sourceCursors;
        std::uint32_t state = 0;
        std::size_t offset = std::numeric_limits<std::size_t>::max();
        double time = 0;
        float speed = 1, fade = 0, fadeTime = 0;
        bool playing = false, loop = true;
        bool dirty = true, initialized = false, visible = true, changed = false;
        Transform occlusionTransform;
        std::uint64_t occlusionFrame = 0;
        unsigned occlusionStreak = 0;
        bool poseThrottled = false;
    };
    explicit Impl(Scene& value, Mode modeIn) : scene(value), mode(modeIn) {}
    Scene& scene;
    const Mode mode;
    AssetReader reader;
    std::unordered_map<std::string, Rig> rigs;
    std::unordered_map<std::string, AnimationClipAsset> clips;
    std::unordered_map<std::string, CachedGraph> graphs;
    std::vector<Instance> instances;
    std::unordered_map<EntityId, std::uint32_t> instanceIndices;
    std::vector<std::uint32_t> evaluation;
    std::vector<std::uint32_t> occlusionCandidates;
    AnimationOcclusionStamp feedback;
    std::uint64_t occlusionGeneration = 0;
    bool occlusionEnabled = false;
    std::unordered_map<EntityId, std::uint64_t> failedInstances;
    std::vector<Mat4> palettes;
    std::string error;
    std::uint64_t revision = 0;
    std::uint64_t poseRevision = 0;
    std::uint64_t layoutRevision = 0;
    std::uint64_t editRevision = 0, destructionRevision = 0, frame = 0;
    std::size_t entityCount = 0;
    int cameraIndex = -1;
    bool layoutDirty = true, running = false;
    WorkerPool workers;

    Instance* find(EntityId id) {
        const auto found = instanceIndices.find(id);
        return found == instanceIndices.end() ? nullptr : &instances[found->second];
    }
    const Instance* meshOwner(const Entity& mesh) const {
        if (mesh.hasAnimator) {
            const auto found = instanceIndices.find(mesh.id());
            return found == instanceIndices.end() ? nullptr : &instances[found->second];
        }
        if (mesh.mesh.meshNode == 0 || instances.empty()) { return nullptr; }
        const auto& path = mesh.mesh.meshPath;
        for (const auto* parent = scene.entityById(mesh.parentId()); parent && isPartOfRig(*parent, path);
            parent = scene.entityById(parent->parentId())) {
            if (!parent->hasAnimator) { continue; }
            const auto found = instanceIndices.find(parent->id());
            if (found != instanceIndices.end() && instances[found->second].meshPath == path) {
                return &instances[found->second];
            }
        }
        return nullptr;
    }
    void remove(EntityId id) {
        const auto found = instanceIndices.find(id);
        if (found == instanceIndices.end()) { return; }
        const auto index = found->second;
        if (index != instances.size() - 1) {
            instances[index] = std::move(instances.back());
            instanceIndices.find(instances[index].id)->second = index;
        }
        instances.pop_back();
        instanceIndices.erase(found);
        ++layoutRevision;
        layoutDirty = true;
    }

    bool read(std::string_view path, std::string& bytes) {
        if (!reader || reader(path, bytes, error) != AssetReadResult::Found) {
            error = "Cannot read animation asset " + std::string(path) + ": " + error; return false;
        }
        return true;
    }
    Rig* rig(std::string_view path) {
        const std::string key(path);
        if (auto found = rigs.find(key); found != rigs.end()) { return &found->second; }
        std::string bytes;
        Rig candidate;
        if (!read(path, bytes) || !decodeGltfAnimation(bytes, path, reader, candidate.data, error)) { return nullptr; }
        std::string metadata;
        const auto metadataResult = reader(key + ".satimport", metadata, error);
        AssetImportSettings settings;
        if (metadataResult == AssetReadResult::Error ||
            (metadataResult == AssetReadResult::Found && !parseImportSettings(metadata, settings, error))) { return nullptr; }
        candidate.scale = settings.scale;
        for (auto& joint : candidate.data.joints) {
            normalizeQuaternion(joint.restTRS.rotation);
            candidate.affine &= isAffine(joint.restLocal) && isAffine(joint.inverseBind);
        }
        // Use the same imported vertex coordinates as the render mesh.
        MeshData mesh;
        std::string boundsError;
        if (importMesh(path, bytes, reader, settings, mesh, boundsError)) {
            candidate.jointBounds = std::move(mesh.jointBounds);
            candidate.nodeOrigins.resize(candidate.data.joints.size() + 1);
            for (const auto& node : mesh.nodes) {
                if (node.id < candidate.nodeOrigins.size()) {
                    candidate.nodeOrigins[node.id] = {node.origin[0], node.origin[1], node.origin[2]};
                }
            }
        }
        // Topological order is cached once. cgltf validates the hierarchy.
        std::vector<bool> visited(candidate.data.joints.size());
        const auto visit = [&](auto&& self, std::size_t index) -> void {
            if (visited[index]) { return; }
            visited[index] = true;
            const int parent = candidate.data.joints[index].parent;
            if (parent >= 0) { self(self, static_cast<std::size_t>(parent)); }
            candidate.order.push_back(index);
        };
        for (std::size_t i = 0; i < visited.size(); ++i) { visit(visit, i); }
        return &rigs.emplace(key, std::move(candidate)).first->second;
    }
    const BoundClip* bind(Instance& instance, std::string_view reference) {
        auto& bindings = instance.rig->bindings;
        const auto& embeddedClips = instance.rig->data.clips;
        if (!embeddedClips.empty() && reference == embeddedClips.front().name && !isAnimationClipAssetPath(reference)) {
            reference = {};
        }
        if (const auto found = bindings.find(reference); found != bindings.end()) { return &found->second.range; }
        const std::string key(reference);
        BoundClip bound;
        const AnimationClip* clip = nullptr;
        std::vector<int> remap;
        if (isAnimationClipAssetPath(reference)) {
            auto found = clips.find(key);
            if (found == clips.end()) {
                std::string bytes;
                AnimationClipAsset asset;
                if (!read(reference, bytes) || !decodeAnimationClip(bytes, asset, error)) { return nullptr; }
                found = clips.emplace(key, std::move(asset)).first;
            }
            const auto& asset = found->second;
            clip = &asset.clip;
            remap = bindAnimationClip(asset, instance.rig->data);
            bound.start = asset.startSeconds; bound.end = asset.endSeconds; bound.speed = asset.speed;
            if (std::find(remap.begin(), remap.end(), -1) != remap.end()) {
                std::cerr << "Animation clip " << key << " has joints absent from " << instance.meshPath << "; those channels are skipped\n";
            }
        } else {
            for (const auto& embedded : embeddedClips) {
                if (reference.empty() || embedded.name == reference) { clip = &embedded; break; }
            }
            if (!clip) { error = "Unknown embedded animation clip " + key; return nullptr; }
            bound.end = clip->duration;
        }
        auto& binding = bindings.emplace(key, Rig::Binding{compileAnimationClip(*clip, remap), bound}).first->second;
        binding.range.clip = &binding.clip;
        return &binding.range;
    }
    bool select(Instance& instance, std::string_view reference, bool loop, float fade) {
        auto* clip = bind(instance, reference);
        if (!clip) { return false; }
        instance.occlusionStreak = 0;
        instance.frozenSource = instance.fadeTime < instance.fade;
        if (fade > 0) {
            if (!instance.clip || instance.frozenSource) {
                instance.source = instance.pose;
                instance.sourceAnimated = instance.animated;
            }
            instance.sourceClip = instance.clip;
            instance.sourceTime = instance.time;
            instance.sourceLoop = instance.loop;
            instance.sourceSpeed = instance.graph ? instance.graph->states[instance.state].speed : 1;
            if (!instance.frozenSource) { instance.sourceCursors = instance.cursors; }
        } else {
            instance.sourceClip = nullptr;
        }
        instance.clip = clip;
        instance.time = 0;
        instance.loop = loop;
        instance.playing = true;
        instance.fade = fade;
        instance.fadeTime = 0;
        instance.dirty = true;
        instance.cursors.assign(clip->clip->tracks.size(), 0);
        return true;
    }
    Instance* ensure(EntityId id) {
        const auto revision = scene.editRevision();
        auto* current = find(id);
        if (current && current->sceneRevision == revision) { return current; }
        const auto failed = failedInstances.find(id);
        if (failed != failedInstances.end() && failed->second == revision) { return nullptr; }
        auto* result = createInstance(id, current);
        if (!result) {
            remove(id);
            failedInstances[id] = revision;
        } else if (failed != failedInstances.end()) {
            failedInstances.erase(failed);
        }
        return result;
    }
    Instance* createInstance(EntityId id, Instance* current) {
        const auto* entity = scene.entityById(id);
        if (!entity || !entity->hasAnimator) { return nullptr; }
        const auto path = animatorMeshPath(scene, *entity);
        if (path.empty()) {
            error = "Animator requires a glTF mesh or a hierarchy from a single glTF asset"; return nullptr;
        }
        if (current && current->meshPath == path && current->settings == entity->animator) {
            current->sceneRevision = scene.editRevision();
            return current;
        }
        Instance instance;
        instance.id = id;
        instance.meshPath = path;
        instance.sceneRevision = scene.editRevision();
        instance.settings = entity->animator;
        instance.speed = entity->animator.speed;
        instance.rig = rig(instance.meshPath);
        if (!instance.rig || instance.rig->data.joints.empty()) { return nullptr; }
        const auto count = instance.rig->data.joints.size();
        instance.pose.reserve(count);
        for (const auto& joint : instance.rig->data.joints) { instance.pose.push_back(joint.restTRS); }
        instance.source = instance.pose;
        instance.worlds.resize(count);
        instance.animated.resize(count);
        instance.sourceAnimated.resize(count);
        if (!entity->animator.controllerPath.empty()) {
            auto graph = graphs.find(entity->animator.controllerPath);
            if (graph == graphs.end()) {
                std::string bytes;
                CachedGraph candidate;
                if (!read(entity->animator.controllerPath, bytes) || !decodeAnimationGraph(bytes, candidate.asset, error)) { return nullptr; }
                candidate.transitions = indexAnimationTransitions(candidate.asset);
                graph = graphs.emplace(entity->animator.controllerPath, std::move(candidate)).first;
            }
            instance.cachedGraph = &graph->second;
            instance.graph = &graph->second.asset;
            instance.state = instance.graph->defaultState;
            for (const auto& parameter : instance.graph->parameters) {
                instance.parameterIndices.emplace(parameter.name, instance.parameters.size());
                instance.parameters.push_back(parameter.value);
            }
            // Load and bind all state clips before updates enter the hot path.
            for (const auto& state : instance.graph->states) { if (!bind(instance, state.clip)) { return nullptr; } }
            const auto& state = instance.graph->states[instance.state];
            if (!select(instance, state.clip, state.loop, 0)) { return nullptr; }
        } else if (!instance.rig->data.clips.empty() || !entity->animator.clip.empty()) {
            if (!select(instance, entity->animator.clip, entity->animator.loop, 0)) { return nullptr; }
        }
        instance.playing = mode == Mode::Runtime && instance.clip && (instance.graph || entity->animator.playOnStart);

        if (current && current->rig == instance.rig) {
            instance.offset = current->offset;
            instance.initialized = current->initialized;
            instance.bounds = current->bounds;
            instance.boundsCenter = current->boundsCenter;
            instance.boundsRadius = current->boundsRadius;
            instance.poseRevision = current->poseRevision;
            *current = std::move(instance);
            return current;
        }
        remove(id);
        const auto index = static_cast<std::uint32_t>(instances.size());
        const auto entry = instanceIndices.emplace(id, index).first;
        try {
            instances.push_back(std::move(instance));
        } catch (...) {
            instanceIndices.erase(entry);
            throw;
        }
        ++layoutRevision;
        layoutDirty = true;
        return &instances.back();
    }
    void reconcile() {
        const auto revision = scene.editRevision();
        const auto destroyed = scene.destructionRevision();
        const auto count = scene.entityCount();
        if (!layoutDirty && revision == editRevision && destroyed == destructionRevision && count == entityCount) { return; }
        for (std::size_t i = 0; i < instances.size();) {
            const auto* entity = scene.entityById(instances[i].id);
            if (!entity || !entity->hasAnimator) { remove(instances[i].id); }
            else { ++i; }
        }
        if (destroyed != destructionRevision) {
            std::erase_if(failedInstances, [&](const auto& entry) { return !scene.entityById(entry.first); });
        }
        const auto& entities = scene.entities();
        for (const auto& entity : entities) {
            if (entity.hasAnimator) { (void)ensure(entity.id()); }
        }
        if (layoutDirty) {
            std::size_t size = 0;
            for (const auto& instance : instances) { size += instance.pose.size(); }
            std::vector<Mat4> relocated(size);
            size = 0;
            for (auto& instance : instances) {
                const auto joints = instance.pose.size();
                if (instance.initialized) {
                    std::copy_n(palettes.data() + instance.offset, joints, relocated.data() + size);
                }
                instance.offset = size;
                size += joints;
            }
            palettes.swap(relocated);
            ++layoutRevision;
            ++poseRevision;
            layoutDirty = false;
        }
        cameraIndex = mode == Mode::Runtime && !instances.empty() ? scene.mainCameraIndex() : -1;
        // Descendants carry independent world transforms. Cull the rig only when every driven mesh is outside.
        for (auto& instance : instances) { instance.meshEntities.clear(); }
        if (cameraIndex >= 0) {
            for (std::size_t i = 0; i < entities.size(); ++i) {
                const auto& entity = entities[i];
                if (!entity.hasMesh) { continue; }
                const auto* owner = meshOwner(entity);
                if (owner && owner->settings.cullingMode == AnimationCullingMode::Automatic) {
                    instances[owner - instances.data()].meshEntities.push_back(i);
                }
            }
        }
        occlusionCandidates.clear();
        for (std::uint32_t i = 0; i < instances.size(); ++i) {
            const auto& instance = instances[i];
            // An independently placed child mesh would need a separate bound.
            if (instance.settings.occlusionRadius > 0 && instance.meshEntities.size() == 1 &&
                entities[instance.meshEntities.front()].id() == instance.id) {
                occlusionCandidates.push_back(i);
            }
        }
        editRevision = revision;
        destructionRevision = destroyed;
        entityCount = count;
    }
    bool matchesFeedback(const AnimationOcclusionStamp& stamp, const Entity& camera) const {
        return stamp.generation == occlusionGeneration && stamp.layout == layoutRevision &&
            stamp.edit == scene.editRevision() && stamp.destruction == scene.destructionRevision() &&
            stamp.frame <= frame && frame - stamp.frame <= kMaxOcclusionAge && matchesCamera(stamp, camera);
    }
    bool boundsFitOcclusionSphere(const Instance& instance) const {
        if (!instance.bounds.valid) { return false; }
        const auto& a = instance.bounds.minimum;
        const auto& b = instance.bounds.maximum;
        const double x = std::max(std::abs(a.x), std::abs(b.x));
        const double y = std::max(std::abs(a.y), std::abs(b.y));
        const double z = std::max(std::abs(a.z), std::abs(b.z));
        const double radius = instance.settings.occlusionRadius;
        return x*x + y*y + z*z <= radius*radius;
    }
    bool occlusionEligible(const Entity& entity) const {
        return entity.hasAnimator && entity.hasMesh && entity.mesh.meshNode == 0 &&
            entity.animator.cullingMode == AnimationCullingMode::Automatic &&
            entity.animator.occlusionRadius > 0 && std::isfinite(entity.animator.occlusionRadius) &&
            !entity.hasCollider && !entity.hasRigidBody;
    }
    unsigned cullingInterval(const Instance& instance, const AnimationFrustum& camera) const {
        if (!instance.bounds.valid || instance.meshEntities.empty()) { return 1; }
        unsigned interval = 0;
        for (const auto index : instance.meshEntities) {
            const auto& entity = scene.entities()[index];
            const auto& origins = instance.rig->nodeOrigins;
            const auto pivot = entity.mesh.meshNode < origins.size() ? origins[entity.mesh.meshNode] : Vec3{};
            const unsigned tier = evaluationInterval(camera, entity.transform, instance.boundsCenter - pivot, instance.boundsRadius);
            if (tier == 1) { return 1; }
            if (tier != 0 && (interval == 0 || tier < interval)) { interval = tier; }
        }
        return interval;
    }
    void sample(const Instance& instance, const BoundClip& bound, double time, bool loop,
        std::vector<std::size_t>& cursors, std::vector<AnimationPose>& poses, std::vector<std::uint8_t>& animated) const {
        const auto& joints = instance.rig->data.joints;
        for (std::size_t i = 0; i < joints.size(); ++i) { poses[i] = joints[i].restTRS; }
        std::fill(animated.begin(), animated.end(), 0);
        const float sample = animationSampleTime(time, bound.start, bound.end, loop);
        const auto& clip = *bound.clip;
        for (std::size_t i = 0; i < clip.tracks.size(); ++i) {
            const auto& track = clip.tracks[i];
            sampleAnimationTrack(clip, track, sample, cursors[i], poses[track.joint], instance.settings.rotationBlend);
            animated[track.joint] = 1;
        }
    }
    void evaluate(Instance& instance) noexcept {
        const auto& joints = instance.rig->data.joints;
        if (instance.clip) {
            sample(instance, *instance.clip, instance.time, instance.loop, instance.cursors, instance.pose, instance.animated);
        }
        if (instance.fadeTime < instance.fade) {
            if (instance.sourceClip && !instance.frozenSource) {
                sample(instance, *instance.sourceClip, instance.sourceTime, instance.sourceLoop, instance.sourceCursors,
                    instance.source, instance.sourceAnimated);
            }
            const float alpha = instance.fadeTime / instance.fade;
            for (std::size_t index = 0; index < joints.size(); ++index) {
                instance.animated[index] |= instance.sourceAnimated[index];
                if (instance.animated[index]) {
                    instance.pose[index] = blendAnimationPose(instance.source[index], instance.pose[index], alpha,
                        instance.settings.rotationBlend);
                }
            }
        }
        bool changed = !instance.initialized;
        for (const auto index : instance.rig->order) {
            const auto& joint = joints[index];
            const auto local = instance.animated[index] ? animationMatrix(instance.pose[index]) : joint.restLocal;
            if (joint.parent < 0) { instance.worlds[index] = local; }
            else {
                const auto& parent = instance.worlds[joint.parent];
                instance.worlds[index] = instance.rig->affine ? affineMultiply(parent, local) : parent * local;
            }
            auto& previous = palettes[instance.offset + index];
            Mat4 palette = instance.rig->affine ? affineMultiply(instance.worlds[index], joint.inverseBind) :
                instance.worlds[index] * joint.inverseBind;
            palette.m[12] *= instance.rig->scale; palette.m[13] *= instance.rig->scale; palette.m[14] *= instance.rig->scale;
            changed |= std::memcmp(previous.m, palette.m, sizeof(palette.m)) != 0;
            previous = palette;
        }
        if (changed) {
            instance.bounds = {};
            const auto& bounds = instance.rig->jointBounds;
            for (std::size_t index = 0; index < bounds.size(); ++index) {
                includeSkinnedBounds(instance.bounds, bounds[index], palettes[instance.offset + index]);
            }
            if (instance.bounds.valid) {
                instance.boundsCenter = (instance.bounds.minimum + instance.bounds.maximum) * 0.5f;
                instance.boundsRadius = length(instance.bounds.maximum - instance.boundsCenter);
            }
        }
        instance.changed = changed;
        instance.poseRevision += instance.changed;
        instance.initialized = true;
        instance.dirty = false;
    }
    bool parameter(EntityId id, std::string_view name, AnimationParameterType type, double value) {
        auto* instance = ensure(id);
        if (!instance || !instance->graph || !std::isfinite(value)) { return false; }
        const auto found = instance->parameterIndices.find(name);
        if (found == instance->parameterIndices.end() || instance->graph->parameters[found->second].type != type) {
            if (instance->reportedParameters.insert(std::string(name)).second) {
                std::cerr << "Entity " << id << ": unknown animation parameter or incorrect type: " << name << '\n';
            }
            return false;
        }
        instance->parameters[found->second] = value;
        return true;
    }
};
AnimationSystem::AnimationSystem(Scene& scene, Mode mode) : m_impl(std::make_unique<Impl>(scene, mode)) {}
AnimationSystem::~AnimationSystem() = default;
void AnimationSystem::setAssetReader(AssetReader reader) {
    stop();
    m_impl->error.clear();
    m_impl->reader = std::move(reader);
}
void AnimationSystem::setAssetRevision(std::uint64_t revision) {
    if (m_impl->revision == revision) { return; }
    const bool active = m_impl->running;
    stop(); m_impl->revision = revision;
    if (active) { (void)start(); }
}
bool AnimationSystem::start() { m_impl->running = true; m_impl->error.clear(); update(0); return m_impl->error.empty(); }
void AnimationSystem::stop() {
    ++m_impl->poseRevision;
    ++m_impl->layoutRevision;
    m_impl->layoutDirty = true;
    m_impl->occlusionCandidates.clear();
    invalidateOcclusion();
    m_impl->instanceIndices.clear();
    m_impl->running = false; m_impl->instances.clear(); m_impl->failedInstances.clear(); m_impl->palettes.clear();
    m_impl->rigs.clear(); m_impl->clips.clear(); m_impl->graphs.clear();
}
void AnimationSystem::update(float deltaSeconds, float timeScale) {
    auto& impl = *m_impl;
    if (!impl.running || !std::isfinite(deltaSeconds) || deltaSeconds < 0 ||
        !std::isfinite(timeScale) || timeScale < 0) { return; }
    impl.reconcile();
    ++impl.frame;
    const double dt = double(deltaSeconds) * timeScale;
    const auto* camera = impl.cameraIndex >= 0 ? impl.scene.entity(static_cast<std::size_t>(impl.cameraIndex)) : nullptr;
    const auto frustum = camera ? animationFrustum(*camera) : AnimationFrustum{};
    const bool validFeedback = impl.occlusionEnabled && camera && !impl.scene.lightingSettings.directionalShadows &&
        impl.feedback.camera != kInvalidEntityId && impl.matchesFeedback(impl.feedback, *camera);
    impl.evaluation.clear();
    for (std::uint32_t index = 0; index < impl.instances.size(); ++index) {
        auto& instance = impl.instances[index];
        if (instance.playing && instance.clip) {
            const float stateSpeed = instance.graph ? instance.graph->states[instance.state].speed : 1;
            const double previousTime = instance.time;
            const float previousFadeTime = instance.fadeTime;
            instance.time += dt * instance.speed * instance.clip->speed * stateSpeed;
            if (instance.fadeTime < instance.fade && instance.sourceClip && !instance.frozenSource) {
                instance.sourceTime += dt * instance.speed * instance.sourceClip->speed * instance.sourceSpeed;
                instance.dirty |= dt != 0;
            }
            instance.fadeTime = static_cast<float>(std::min(double(instance.fade), instance.fadeTime + dt));
            if (instance.graph) {
                const double duration = instance.clip->end - instance.clip->start;
                const int transition = animationTransition(*instance.graph, instance.cachedGraph->transitions, instance.state,
                    duration > 0 ? instance.time / duration : 1, instance.parameters, instance.fadeTime < instance.fade);
                if (transition >= 0) {
                    const auto& edge = instance.graph->transitions[transition];
                    const auto& target = instance.graph->states[edge.target];
                    if (impl.select(instance, target.clip, target.loop, edge.durationSeconds)) {
                        instance.state = edge.target;
                        consumeAnimationTriggers(*instance.graph, edge, instance.parameters);
                    }
                }
            }
            if (!instance.loop && instance.time >= instance.clip->end - instance.clip->start) {
                instance.time = instance.clip->end - instance.clip->start;
                if (!instance.graph) { instance.playing = false; }
            }
            instance.dirty |= instance.time != previousTime || instance.fadeTime != previousFadeTime;
        }
        if (instance.initialized && !instance.dirty) { continue; }
        unsigned interval = camera ? impl.cullingInterval(instance, frustum) : 1;
        bool throttled = false;
        if (impl.occlusionEnabled && instance.settings.occlusionRadius > 0 &&
            instance.settings.cullingMode == AnimationCullingMode::Automatic) {
            // For opted-in rigs, a fresh opaque-depth proof is the only reason
            // to delay the pose. Missing feedback restores full evaluation.
            interval = 1;
            if (validFeedback && instance.occlusionFrame == impl.feedback.frame &&
                instance.occlusionStreak >= 2 && instance.fadeTime >= instance.fade) {
                const auto* entity = impl.scene.entityById(instance.id);
                throttled = entity && impl.occlusionEligible(*entity) &&
                    sameTransform(instance.occlusionTransform, entity->transform) &&
                    impl.boundsFitOcclusionSphere(instance);
                if (throttled) { interval = kOcclusionInterval; }
            }
        }
        const bool becameVisible = interval != 0 && !instance.visible;
        const bool resumed = instance.poseThrottled && !throttled;
        instance.visible = interval != 0;
        instance.poseThrottled = throttled;
        if (!instance.initialized || (interval != 0 && (resumed || becameVisible || (impl.frame + index) % interval == 0))) {
            impl.evaluation.push_back(index);
        }
    }
    const auto evaluate = [](void* context, std::size_t job) noexcept {
        auto& impl = *static_cast<Impl*>(context);
        impl.evaluate(impl.instances[impl.evaluation[job]]);
    };
    constexpr std::size_t kParallelThreshold = 8;
    if (impl.evaluation.size() >= kParallelThreshold) {
        impl.workers.parallelFor(impl.evaluation.size(), &impl, evaluate);
    } else {
        for (std::size_t i = 0; i < impl.evaluation.size(); ++i) { evaluate(&impl, i); }
    }
    bool changed = false;
    for (const auto index : impl.evaluation) { changed |= impl.instances[index].changed; }
    impl.poseRevision += changed;
}
bool AnimationSystem::play(EntityId id, std::string_view reference, bool loop, float fade) {
    m_impl->running = true;
    if (!std::isfinite(fade) || fade < 0) { return false; }
    auto* instance = m_impl->ensure(id);
    if (!instance || !m_impl->select(*instance, reference, loop, fade)) { return false; }
    instance->graph = nullptr;
    return true;
}
bool AnimationSystem::resume(EntityId id) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !instance->clip) { return false; }
    m_impl->running = true;
    instance->playing = true;
    instance->occlusionStreak = 0;
    return true;
}
void AnimationSystem::stop(EntityId id) { if (auto* instance = m_impl->find(id)) { instance->playing = false; } }
bool AnimationSystem::setSpeed(EntityId id, float speed) {
    auto* instance = m_impl->ensure(id); if (!instance || !std::isfinite(speed)) { return false; } instance->speed = speed; return true;
}
bool AnimationSystem::setTime(EntityId id, float time) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !std::isfinite(time)) { return false; }
    instance->occlusionStreak = 0;
    instance->dirty |= instance->time != time;
    instance->time = time;
    if (m_impl->mode == Mode::Preview) { m_impl->running = true; }
    return true;
}
float AnimationSystem::time(EntityId id) const { const auto* instance = m_impl->find(id); return instance ? static_cast<float>(instance->time) : 0; }
bool AnimationSystem::isPlaying(EntityId id) const { const auto* instance = m_impl->find(id); return instance && instance->playing; }
std::vector<std::string> AnimationSystem::clipNames(std::string_view path) {
    std::vector<std::string> names;
    if (path.empty()) { return names; }
    if (const auto* rig = m_impl->rig(path)) { for (const auto& clip : rig->data.clips) { names.push_back(clip.name); } }
    return names;
}
std::span<const Mat4> AnimationSystem::palette(EntityId id) const {
    const auto* found = m_impl->find(id);
    if (!found || !found->initialized) { return {}; }
    const auto& instance = *found;
    const auto size = m_impl->palettes.size();
    if (instance.offset > size || instance.pose.size() > size - instance.offset) { return {}; }
    return {m_impl->palettes.data() + instance.offset, instance.pose.size()};
}
std::span<const Mat4> AnimationSystem::meshPalette(const Entity& meshEntity) const {
    return meshPose(meshEntity).palette;
}
std::uint64_t AnimationSystem::poseRevision() const { return m_impl->poseRevision; }
void AnimationSystem::enableOcclusionFeedback() {
    m_impl->occlusionEnabled = m_impl->mode == Mode::Runtime;
}

AnimationOcclusionStamp AnimationSystem::collectOcclusionProbes(std::vector<AnimationOcclusionProbe>& probes) const {
    const auto& impl = *m_impl;
    probes.clear();
    if (!impl.occlusionEnabled || !impl.running || impl.occlusionCandidates.empty() ||
        impl.scene.lightingSettings.directionalShadows || impl.editRevision != impl.scene.editRevision() ||
        impl.destructionRevision != impl.scene.destructionRevision() || impl.cameraIndex < 0) { return {}; }
    const auto* camera = impl.scene.entity(static_cast<std::size_t>(impl.cameraIndex));
    if (!camera || !camera->hasCamera) { return {}; }
    for (const auto index : impl.occlusionCandidates) {
        const auto& instance = impl.instances[index];
        const auto* entity = impl.scene.entityById(instance.id);
        if (!entity || !impl.occlusionEligible(*entity) || !instance.playing ||
            !impl.boundsFitOcclusionSphere(instance)) { continue; }
        const auto radius = animationOcclusionWorldRadius(*entity);
        if (radius > 0 && std::isfinite(radius)) { probes.push_back({entity->id(), entity->transform, radius}); }
    }
    return {impl.frame, impl.occlusionGeneration, impl.layoutRevision, impl.editRevision, impl.destructionRevision,
        camera->id(), camera->transform, camera->camera.fieldOfView, camera->camera.nearClip, camera->camera.farClip};
}

void AnimationSystem::submitOcclusion(const AnimationOcclusionStamp& stamp,
    std::span<const AnimationOcclusionProbe> probes, std::span<const std::uint32_t> occluded) {
    auto& impl = *m_impl;
    if (!impl.occlusionEnabled || probes.size() != occluded.size() || stamp.frame <= impl.feedback.frame ||
        impl.scene.lightingSettings.directionalShadows || impl.cameraIndex < 0) { return; }
    const auto* camera = impl.scene.entity(static_cast<std::size_t>(impl.cameraIndex));
    if (!camera || !camera->hasCamera || !impl.matchesFeedback(stamp, *camera)) { return; }
    const bool consecutive = impl.feedback.camera != kInvalidEntityId && impl.matchesFeedback(impl.feedback, *camera);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const auto& probe = probes[i];
        if (auto* instance = impl.find(probe.entity)) {
            const bool same = consecutive && instance->occlusionFrame == impl.feedback.frame &&
                sameTransform(instance->occlusionTransform, probe.transform);
            instance->occlusionFrame = stamp.frame;
            instance->occlusionTransform = probe.transform;
            instance->occlusionStreak = occluded[i] ? (same ? std::min(instance->occlusionStreak + 1, 2u) : 1) : 0;
        }
    }
    impl.feedback = stamp;
}

void AnimationSystem::invalidateOcclusion() {
    ++m_impl->occlusionGeneration;
    m_impl->feedback = {};
}

bool AnimationSystem::refreshOcclusionPoses(bool geometryMoved) {
    auto& impl = *m_impl;
    if (!impl.occlusionEnabled || impl.occlusionCandidates.empty()) { return false; }
    if (impl.feedback.camera == kInvalidEntityId) {
        if (geometryMoved) { invalidateOcclusion(); }
        return false;
    }
    const auto* camera = impl.cameraIndex >= 0 ? impl.scene.entity(static_cast<std::size_t>(impl.cameraIndex)) : nullptr;
    if (!geometryMoved && !impl.scene.lightingSettings.directionalShadows && camera && camera->hasCamera &&
        impl.matchesFeedback(impl.feedback, *camera)) { return false; }
    invalidateOcclusion();
    if (impl.layoutDirty) { return false; }
    bool changed = false;
    for (const auto index : impl.occlusionCandidates) {
        auto& instance = impl.instances[index];
        if (!instance.poseThrottled) { continue; }
        instance.poseThrottled = false;
        if (instance.dirty) {
            impl.evaluate(instance);
            changed |= instance.changed;
        }
    }
    impl.poseRevision += changed;
    return changed;
}

std::span<const Mat4> AnimationSystem::palettes() const { return m_impl->palettes; }
std::uint64_t AnimationSystem::layoutRevision() const { return m_impl->layoutRevision; }
AnimationBounds AnimationSystem::bounds(AnimationInstanceHandle handle) const {
    if (handle.layoutRevision != m_impl->layoutRevision || handle.index >= m_impl->instances.size()) { return {}; }
    return m_impl->instances[handle.index].bounds;
}
std::uint64_t AnimationSystem::poseRevision(AnimationInstanceHandle handle) const {
    if (handle.layoutRevision != m_impl->layoutRevision || handle.index >= m_impl->instances.size()) { return 0; }
    return m_impl->instances[handle.index].poseRevision;
}
AnimationMeshPose AnimationSystem::meshPose(const Entity& meshEntity) const {
    const auto* instance = m_impl->meshOwner(meshEntity);
    if (!instance || !instance->initialized) { return {}; }
    const auto size = m_impl->palettes.size();
    if (instance->offset > size || instance->pose.size() > size - instance->offset) { return {}; }
    const auto index = static_cast<std::uint32_t>(instance - m_impl->instances.data());
    return {{m_impl->palettes.data() + instance->offset, instance->pose.size()}, instance->bounds,
        static_cast<std::uint32_t>(instance->offset), {m_impl->layoutRevision, index}};
}
bool AnimationSystem::setBool(EntityId id, std::string_view name, bool value) { return m_impl->parameter(id, name, AnimationParameterType::Bool, value); }
bool AnimationSystem::setFloat(EntityId id, std::string_view name, float value) { return m_impl->parameter(id, name, AnimationParameterType::Float, value); }
bool AnimationSystem::setInt(EntityId id, std::string_view name, std::int32_t value) { return m_impl->parameter(id, name, AnimationParameterType::Int, value); }
bool AnimationSystem::setTrigger(EntityId id, std::string_view name) { return m_impl->parameter(id, name, AnimationParameterType::Trigger, 1); }
std::string_view AnimationSystem::currentState(EntityId id) const {
    const auto* instance = m_impl->find(id);
    return instance && instance->graph ? instance->graph->states[instance->state].name : std::string_view{};
}
bool AnimationSystem::crossfadeTo(EntityId id, std::string_view name, float seconds) {
    auto* instance = m_impl->ensure(id);
    if (!instance || !instance->graph || !std::isfinite(seconds) || seconds < 0) { return false; }
    for (std::size_t index = 0; index < instance->graph->states.size(); ++index) {
        const auto& state = instance->graph->states[index];
        if (state.name == name && m_impl->select(*instance, state.clip, state.loop, seconds)) {
            instance->state = static_cast<std::uint32_t>(index); return true;
        }
    }
    return false;
}
AnimationDebugView AnimationSystem::debugView(EntityId id) const {
    const auto* found = m_impl->find(id);
    if (!found) { return {}; }
    const auto& instance = *found;
    return {currentState(id), instance.fade > 0 ? std::min(instance.fadeTime / instance.fade, 1.0f) : 1, instance.parameters};
}
const std::string& AnimationSystem::error() const { return m_impl->error; }
} // namespace sat
