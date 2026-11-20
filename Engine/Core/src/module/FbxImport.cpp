#include "FbxImport.hpp"

#include "AnimationAsset.hpp"
#include "MeshGeometry.hpp"

#include <ufbx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sat {
namespace {
using FbxScene = std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)>;

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

std::string stringOf(ufbx_string value) {
    return std::string(value.data, value.length);
}

FbxScene load(std::string_view bytes, std::string& error) {
    FbxScene scene(nullptr, ufbx_free_scene);
    if (bytes.empty() || bytes.size() > kMaxAssetBytes) {
        error = "Invalid FBX source size";
        return scene;
    }
    ufbx_load_opts options{};
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.target_axes = ufbx_axes_left_handed_y_up;
    // Mirror depth explicitly so a handedness change preserves the authored up axis
    // in both geometry and evaluated TRS transforms.
    options.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;
    options.target_unit_meters = 1;
    options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_HELPER_NODES;
    options.generate_missing_normals = true;
    options.node_depth_limit = 256;
    options.temp_allocator.memory_limit = 4 * kMaxAssetBytes;
    options.result_allocator.memory_limit = 4 * kMaxAssetBytes;
    // External files are resolved separately through AssetReader.
    ufbx_error parseError{};
    scene.reset(ufbx_load_memory(bytes.data(), bytes.size(), &options, &parseError));
    if (!scene) {
        char message[512]{};
        ufbx_format_error(message, sizeof(message), &parseError);
        error = message;
    }
    return scene;
}

Vec3 vectorOf(ufbx_vec3 value) {
    return {float(value.x), float(value.y), float(value.z)};
}

Mat4 matrixOf(const ufbx_matrix& source) {
    Mat4 matrix;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 3; ++row) { matrix.m[column * 4 + row] = float(source.v[column * 3 + row]); }
    }
    return matrix;
}

AnimationPose poseOf(const ufbx_transform& transform) {
    const auto q = transform.rotation;
    return {vectorOf(transform.translation), {float(q.x), float(q.y), float(q.z), float(q.w)}, vectorOf(transform.scale)};
}

struct Model {
    explicit Model(const ufbx_scene& scene) : source(scene), slots(scene.nodes.count), bindings(scene.nodes.count) {
        nodes.push_back(scene.root_node);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto* node = nodes[i];
            slots[node->typed_id] = static_cast<std::uint32_t>(i);
            for (const auto* child : node->children) { nodes.push_back(child); }
        }
    }

    bool hasRig() const {
        return source.anim_stacks.count != 0 || source.skin_deformers.count != 0;
    }

    bool buildRig(std::string& error) {
        if (!hasRig()) { return true; }
        if (nodes.size() > kMaxAnimationJoints) { return fail(error, "FBX rig exceeds the 256-joint limit"); }
        std::unordered_set<std::string> names;
        for (const auto* node : nodes) {
            AnimationJoint joint;
            joint.name = stringOf(node->name);
            const auto base = joint.name.empty() ? "Node" : joint.name;
            for (unsigned suffix = 1; joint.name.empty() || !names.insert(joint.name).second; ++suffix) {
                joint.name = base + " #" + std::to_string(suffix);
            }
            joint.parent = node->parent ? int(slots[node->parent->typed_id]) : -1;
            joint.restTRS = poseOf(node->local_transform);
            joint.restLocal = matrixOf(node->node_to_parent);
            joint.inverseBind = matrixOf(ufbx_matrix_invert(&node->node_to_world));
            rig.joints.push_back(std::move(joint));
            restWorld.push_back(matrixOf(node->node_to_world));
        }

        for (const auto* node : nodes) {
            if (!node->mesh || !node->mesh->skin_deformers.count) { continue; }
            const auto& skins = node->mesh->skin_deformers;
            if (skins.count != 1 || (skins[0]->skinning_method != UFBX_SKINNING_METHOD_LINEAR &&
                skins[0]->skinning_method != UFBX_SKINNING_METHOD_RIGID)) {
                return fail(error, "FBX meshes require a single linear or rigid skin deformer");
            }
            const auto worldToGeometry = ufbx_matrix_invert(&node->geometry_to_world);
            auto& binding = bindings[node->typed_id];
            for (const auto* cluster : skins[0]->clusters) {
                const auto bone = slots[cluster->bone_node->typed_id];
                // Vertices are already in model space. Undo that bake before applying the FBX cluster bind.
                const auto inverseBind = matrixOf(ufbx_matrix_mul(&cluster->geometry_to_bone, &worldToGeometry));
                std::size_t joint = bone;
                while (joint < rig.joints.size()) {
                    if ((joint == bone || (joint >= nodes.size() && rig.joints[joint].parent == int(bone))) &&
                        std::equal(std::begin(inverseBind.m), std::end(inverseBind.m),
                            std::begin(rig.joints[joint].inverseBind.m))) { break; }
                    ++joint;
                }
                if (joint == rig.joints.size()) {
                    if (joint == kMaxAnimationJoints) { return fail(error, "FBX skin bindings exceed 256 palette joints"); }
                    AnimationJoint alias;
                    alias.name = rig.joints[bone].name + " (bind " + std::to_string(joint) + ")";
                    while (!names.insert(alias.name).second) { alias.name += '_'; }
                    alias.parent = int(bone);
                    alias.inverseBind = inverseBind;
                    rig.joints.push_back(std::move(alias));
                    restWorld.push_back(restWorld[bone]);
                }
                binding.push_back(static_cast<std::uint8_t>(joint));
            }
        }
        for (std::size_t i = 0; i < rig.joints.size(); ++i) {
            for (const auto& matrix : {rig.joints[i].restLocal, rig.joints[i].inverseBind, restWorld[i]}) {
                for (float value : matrix.m) {
                    if (!std::isfinite(value)) { return fail(error, "Nonfinite FBX rig transform"); }
                }
            }
        }
        return true;
    }

    bool readClips(bool bake, std::string& error) {
        const double sourceRate = source.settings.frames_per_second;
        const double fps = std::isfinite(sourceRate) && sourceRate > 0 ? sourceRate : 30;
        std::size_t remaining = kMaxAnimationKeys;
        for (const auto* stack : source.anim_stacks) {
            const double duration = stack->time_end - stack->time_begin;
            const double intervals = std::ceil(duration * fps);
            const auto keysPerFrame = nodes.size() * 3;
            if (!std::isfinite(intervals) || duration < 0 || intervals >= double(remaining / keysPerFrame) ||
                !std::isfinite(float(duration))) {
                return fail(error, "Invalid FBX take duration or animation key limit exceeded");
            }
            const auto frames = static_cast<std::size_t>(intervals) + 1;
            remaining -= frames * keysPerFrame;
            if (!bake) { continue; }

            AnimationClipAsset asset;
            asset.clip.name = stack->name.length ? stringOf(stack->name) : "Take " + std::to_string(stack->typed_id + 1);
            asset.clip.duration = asset.endSeconds = float(duration);
            for (const auto& joint : rig.joints) { asset.jointNames.push_back(joint.name); }
            for (std::size_t j = 0; j < nodes.size(); ++j) {
                std::array<AnimationChannel, 3> channels;
                for (std::size_t axis = 0; axis < channels.size(); ++axis) {
                    auto& channel = channels[axis];
                    channel.joint = static_cast<std::uint16_t>(j);
                    channel.path = static_cast<AnimationPath>(axis);
                    channel.times.reserve(frames);
                    channel.values.reserve(frames * (axis == 1 ? 4 : 3));
                }
                for (std::size_t frame = 0; frame < frames; ++frame) {
                    const double time = frame + 1 == frames ? duration : double(frame) / fps;
                    const auto* node = nodes[j];
                    const float sample = float(time);
                    if (!channels[0].times.empty() && sample <= channels[0].times.back()) { continue; }
                    const auto pose = poseOf(node->parent
                        ? ufbx_evaluate_transform(stack->anim, node, stack->time_begin + time) : node->local_transform);
                    for (auto& channel : channels) { channel.times.push_back(sample); }
                    channels[0].values.insert(channels[0].values.end(), {pose.translation.x, pose.translation.y, pose.translation.z});
                    channels[1].values.insert(channels[1].values.end(), std::begin(pose.rotation), std::end(pose.rotation));
                    channels[2].values.insert(channels[2].values.end(), {pose.scale.x, pose.scale.y, pose.scale.z});
                }
                for (auto& channel : channels) { asset.clip.channels.push_back(std::move(channel)); }
            }
            if (!validateAnimationClip(asset, error)) { return false; }
            rig.clips.push_back(std::move(asset.clip));
        }
        return true;
    }

    MeshSkinVertex skinVertex(const ufbx_node& node, unsigned vertex) const {
        MeshSkinVertex result;
        const auto& binding = bindings[node.typed_id];
        const auto rigid = [&] {
            result.joints[0] = static_cast<std::uint8_t>(slots[node.typed_id]);
            result.weights[0] = 255;
            return result;
        };
        if (binding.empty()) { return rigid(); }
        const auto& skin = *node.mesh->skin_deformers[0];
        const auto& sourceVertex = skin.vertices[vertex];
        // ufbx orders influences from strongest to weakest.
        const auto count = std::min<std::size_t>(sourceVertex.num_weights, 4);
        double total = 0;
        for (std::size_t i = 0; i < count; ++i) { total += skin.weights[sourceVertex.weight_begin + i].weight; }
        if (!(total > 0) || !std::isfinite(total)) { return rigid(); }
        unsigned packed = 0;
        double cumulative = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& weight = skin.weights[sourceVertex.weight_begin + i];
            cumulative += weight.weight;
            const auto end = i + 1 == count ? 255u : static_cast<unsigned>(std::lround(255 * cumulative / total));
            result.joints[i] = binding[weight.cluster_index];
            result.weights[i] = static_cast<std::uint8_t>(end - packed);
            packed = end;
        }
        return result;
    }

    const ufbx_scene& source;
    std::vector<const ufbx_node*> nodes;
    std::vector<std::uint32_t> slots;
    std::vector<std::vector<std::uint8_t>> bindings;
    std::vector<Mat4> restWorld;
    AnimationData rig;
};

void expand(AnimationBounds& bounds, Vec3 value) {
    if (!bounds.valid) {
        bounds = {value, value, true};
        return;
    }
    bounds.minimum = {std::min(bounds.minimum.x, value.x), std::min(bounds.minimum.y, value.y), std::min(bounds.minimum.z, value.z)};
    bounds.maximum = {std::max(bounds.maximum.x, value.x), std::max(bounds.maximum.y, value.y), std::max(bounds.maximum.z, value.z)};
}

bool appendGeometry(const Model& model, const ufbx_node& node, bool flipV, MeshData& mesh,
    std::vector<Vec3>& bitangents, AnimationBounds& bounds, std::string& error) {
    if (!node.mesh) { return true; }
    const auto& source = *node.mesh;
    if (source.num_indices > meshGeometry::kMaxIndices || source.num_triangles > meshGeometry::kMaxIndices / 3) {
        return fail(error, "FBX mesh exceeds index limit");
    }
    const auto normalMatrix = ufbx_matrix_for_normals(&node.geometry_to_world);
    const bool reverse = ufbx_matrix_determinant(&node.geometry_to_world) < 0;
    std::vector<std::uint32_t> triangles;
    std::vector<std::uint32_t> corners(source.num_indices, UFBX_NO_INDEX);
    for (const auto& part : source.material_parts) {
        const auto first = static_cast<std::uint32_t>(mesh.indices.size());
        for (const auto faceIndex : part.face_indices) {
            const auto face = source.faces[faceIndex];
            if (face.num_indices < 3) { continue; }
            triangles.resize(std::size_t(face.num_indices - 2) * 3);
            const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &source, face);
            if (!count) { return fail(error, "Cannot triangulate FBX polygon"); }
            if (count > (meshGeometry::kMaxIndices - mesh.indices.size()) / 3) {
                return fail(error, "FBX mesh exceeds index limit");
            }
            for (std::size_t i = 0; i < std::size_t(count) * 3; i += 3) {
                if (reverse) { std::swap(triangles[i + 1], triangles[i + 2]); }
                for (std::size_t j = i; j < i + 3; ++j) {
                    const auto corner = triangles[j];
                    auto& index = corners[corner];
                    if (index == UFBX_NO_INDEX) {
                        if (mesh.vertices.size() == meshGeometry::kMaxVertices) {
                            return fail(error, "FBX mesh exceeds vertex limit");
                        }
                        const auto position = vectorOf(ufbx_transform_position(&node.geometry_to_world, source.vertex_position[corner]));
                        const auto normal = meshGeometry::unitVector(vectorOf(ufbx_transform_direction(&normalMatrix, source.vertex_normal[corner])));
                        const auto uv = source.vertex_uv.exists ? source.vertex_uv[corner] : ufbx_vec2{};
                        MeshVertex vertex{position.x, position.y, position.z, normal.x, normal.y, normal.z,
                            float(uv.x), float(source.vertex_uv.exists && flipV ? 1 - uv.y : uv.y), 0, 0, 0, 0};
                        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z) ||
                            !std::isfinite(vertex.u) || !std::isfinite(vertex.v)) {
                            return fail(error, "Nonfinite FBX vertex");
                        }
                        if (source.vertex_color.exists) {
                            const auto color = source.vertex_color[corner];
                            for (int c = 0; c < 4; ++c) { vertex.color[c] = float(color.v[c]); }
                        }
                        index = static_cast<std::uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back(vertex);
                        bitangents.emplace_back();
                        expand(bounds, position);
                        if (model.hasRig()) {
                            const auto skin = model.skinVertex(node, source.vertex_indices[corner]);
                            mesh.skinVertices.push_back(skin);
                            for (int influence = 0; influence < 4; ++influence) {
                                if (skin.weights[influence]) { expand(mesh.jointBounds[skin.joints[influence]], position); }
                            }
                        }
                    }
                    mesh.indices.push_back(index);
                }
                meshGeometry::accumulateTangent(mesh, bitangents,
                    corners[triangles[i]], corners[triangles[i + 1]], corners[triangles[i + 2]]);
            }
        }
        const auto count = static_cast<std::uint32_t>(mesh.indices.size()) - first;
        if (count) {
            const auto* material = part.index < node.materials.count ? node.materials[part.index] : nullptr;
            mesh.sections.push_back({first, count, material ? material->typed_id : kNoMeshMaterial});
        }
    }
    return true;
}

std::string resolveTexture(const ufbx_texture* source, std::string_view path, const AssetReader& reader,
    MeshData& mesh, std::size_t& embeddedBytes) {
    if (!source) { return {}; }
    if (source->type != UFBX_TEXTURE_FILE) {
        if (!source->file_textures.count) { return {}; }
        source = source->file_textures[0];
    }
    const auto folder = path.substr(0, path.find_last_of('/') + 1);
    std::string filename = stringOf(source->relative_filename);
    if (filename.empty()) { filename = stringOf(source->filename); }
    std::replace(filename.begin(), filename.end(), '\\', '/');
    auto imagePath = std::string(folder) + filename;
    std::string bytes, readError;
    if (source->content.size) {
        const std::string_view content(static_cast<const char*>(source->content.data), source->content.size);
        const char* extension = content.starts_with("\x89PNG\r\n\x1a\n") ? ".png" :
            content.starts_with("\xff\xd8\xff") ? ".jpg" : nullptr;
        if (extension && content.size() <= kMaxAssetBytes) {
            const auto imageId = source->video ? source->video->element_id : source->element_id;
            imagePath = std::string(path) + "_image_" + std::to_string(imageId) + extension;
            if (isTextureAssetPath(imagePath)) {
                if (std::ranges::any_of(mesh.embeddedImages, [&](const auto& image) { return image.path == imagePath; })) {
                    return imagePath;
                }
                if (content.size() <= 2 * kMaxAssetBytes - embeddedBytes) {
                    embeddedBytes += content.size();
                    mesh.embeddedImages.push_back({imagePath, std::string(content)});
                    return imagePath;
                }
            }
        }
    } else if (isTextureAssetPath(imagePath) && reader) {
        if (std::ranges::find(mesh.dependencies, imagePath) != mesh.dependencies.end()) { return imagePath; }
        if (reader(imagePath, bytes, readError) == AssetReadResult::Found) {
            mesh.dependencies.push_back(imagePath);
            return imagePath;
        }
    }
    if (mesh.materialWarning.empty()) { mesh.materialWarning = "Cannot import FBX texture " + filename; }
    return {};
}

void readMaterials(const ufbx_scene& source, std::string_view path, const AssetReader& reader, MeshData& mesh) {
    std::size_t embeddedBytes = 0;
    for (const auto* input : source.materials) {
        const auto& maps = input->fbx;
        MaterialData material;
        for (int axis = 0; axis < 3; ++axis) {
            if (maps.diffuse_color.has_value) {
                material.baseColor[axis] = float(std::clamp(maps.diffuse_color.value_vec3.v[axis] *
                    (maps.diffuse_factor.has_value ? maps.diffuse_factor.value_real : 1.0), 0.0, 1.0));
            }
            if (maps.emission_color.has_value) {
                material.emissive[axis] = float(std::clamp(maps.emission_color.value_vec3.v[axis] *
                    (maps.emission_factor.has_value ? maps.emission_factor.value_real : 1.0), 0.0, 1.0));
            }
        }
        if (input->pbr.opacity.has_value) {
            material.baseColor[3] = float(std::clamp(input->pbr.opacity.value_real, 0.0, 1.0));
        }
        if (material.baseColor[3] < 1) { material.alphaMode = AlphaMode::Blend; }
        material.baseColorTexture = resolveTexture(maps.diffuse_color.texture ? maps.diffuse_color.texture : input->pbr.base_color.texture,
            path, reader, mesh, embeddedBytes);
        material.normalTexture = resolveTexture(maps.normal_map.texture ? maps.normal_map.texture : input->pbr.normal_map.texture,
            path, reader, mesh, embeddedBytes);
        mesh.materials.push_back(std::move(material));
        mesh.materialNames.push_back(stringOf(input->name));
    }
}
}

bool decodeFbxMesh(std::string_view path, std::string_view bytes, const AssetReader& reader,
    bool flipV, MeshData& mesh, std::string& error) {
    const auto scene = load(bytes, error);
    if (!scene) { return false; }
    Model model(*scene);
    if (!model.buildRig(error) || !model.readClips(false, error)) { return false; }

    MeshData candidate;
    std::vector<Vec3> bitangents;
    candidate.jointBounds.resize(model.rig.joints.size());
    for (std::size_t i = 0; i < model.rig.joints.size(); ++i) {
        candidate.restPalette.push_back(model.restWorld[i] * model.rig.joints[i].inverseBind);
    }
    for (std::size_t i = 1; i < model.nodes.size(); ++i) {
        const auto& node = *model.nodes[i];
        MeshNode part;
        part.id = static_cast<std::uint32_t>(i + 1);
        part.parent = node.parent == scene->root_node ? 0 : model.slots[node.parent->typed_id] + 1;
        part.name = node.name.length ? stringOf(node.name) : "Node " + std::to_string(part.id);
        part.firstSection = static_cast<std::uint32_t>(candidate.sections.size());
        const auto origin = node.node_to_world.cols[3];
        part.origin[0] = float(origin.x);
        part.origin[1] = float(origin.y);
        part.origin[2] = float(origin.z);
        AnimationBounds bounds;
        if (!appendGeometry(model, node, flipV, candidate, bitangents, bounds, error)) { return false; }
        part.sectionCount = static_cast<std::uint32_t>(candidate.sections.size()) - part.firstSection;
        if (bounds.valid) {
            part.minimum[0] = bounds.minimum.x; part.maximum[0] = bounds.maximum.x;
            part.minimum[1] = bounds.minimum.y; part.maximum[1] = bounds.maximum.y;
            part.minimum[2] = bounds.minimum.z; part.maximum[2] = bounds.maximum.z;
        }
        candidate.nodes.push_back(std::move(part));
    }
    if (candidate.indices.empty()) { return fail(error, "No triangles found in FBX scene"); }
    meshGeometry::finalizeMesh(candidate, bitangents);
    readMaterials(*scene, path, reader, candidate);
    mesh = std::move(candidate);
    error.clear();
    return true;
}

bool decodeFbxAnimation(std::string_view bytes, AnimationData& animation, std::string& error) {
    const auto scene = load(bytes, error);
    if (!scene) { return false; }
    Model model(*scene);
    if (!model.buildRig(error) || !model.readClips(true, error)) { return false; }
    animation = std::move(model.rig);
    error.clear();
    return true;
}
}
