#include "SceneFile.hpp"

#include "Scene.hpp"
#include "ScriptFields.hpp"
#include "module/SceneAudioFile.hpp"
#include "module/ScenePhysicsFile.hpp"
#include "AssetData.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <iostream>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace sat {
namespace {
constexpr int kSceneVersionWithEntityReferences = 14;
constexpr int kSceneVersionWithScriptFields = 13;
constexpr int kSceneVersionWithUi = 12;
constexpr int kSceneVersionWithoutUi = 11;
constexpr std::size_t kMaxSceneBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxEntities = 100000;
constexpr std::size_t kMaxScripts = 128;

bool finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const DVec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool readScriptField(std::istream& in, ScriptFields& fields, int version, std::size_t entityCount) {
    std::string name;
    char type = 0;
    if (!(in >> std::quoted(name) >> type) || !isValidScriptFieldName(name) || fields.contains(name)) {
        return false;
    }
    ScriptFieldValue value;
    switch (type) {
    case 'b': {
        bool boolean = false;
        if (!(in >> boolean)) { return false; }
        value = boolean;
        break;
    }
    case 'i': {
        std::int64_t integer = 0;
        if (!(in >> integer)) { return false; }
        value = integer;
        break;
    }
    case 'n': {
        double number = 0;
        if (!(in >> number) || !std::isfinite(number)) { return false; }
        value = number;
        break;
    }
    case 's': {
        std::string text;
        if (!(in >> std::quoted(text)) || !isValidScriptFieldText(text)) { return false; }
        value = std::move(text);
        break;
    }
    case 'e': {
        std::uint64_t ordinal = 0;
        if (version < kSceneVersionWithEntityReferences || !(in >> ordinal) || ordinal > entityCount) {
            return false;
        }
        // Resolve the file-local ordinal once all entities exist.
        value = ScriptEntityReference{ordinal};
        break;
    }
    default:
        return false;
    }
    fields.emplace(std::move(name), std::move(value));
    return true;
}

void writeScriptFields(std::ostream& out, const ScriptFields& fields, const Scene& scene) {
    out << fields.size() << '\n';
    for (const auto& [name, value] : fields) {
        out << std::quoted(name) << ' ';
        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                out << "b " << item;
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                out << "i " << item;
            } else if constexpr (std::is_same_v<T, double>) {
                out << "n " << std::setprecision(std::numeric_limits<double>::max_digits10) << item
                    << std::setprecision(std::numeric_limits<float>::max_digits10);
            } else if constexpr (std::is_same_v<T, ScriptEntityReference>) {
                const auto index = scene.entityIndex(item.entityId);
                out << "e " << (index ? *index + 1 : 0);
            } else {
                out << "s " << std::quoted(item);
            }
        }, value);
        out << '\n';
    }
}
} // namespace

std::string serializeScene(const Scene& scene) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool hasUi = std::any_of(scene.entities().begin(), scene.entities().end(),
        [](const Entity& entity) { return entity.gameUi.has_value(); });
    int version = hasUi ? kSceneVersionWithUi : kSceneVersionWithoutUi;
    for (const auto& entity : scene.entities()) {
        for (const auto& script : entity.scripts) {
            if (!script.fieldOverrides.empty()) {
                version = std::max(version, kSceneVersionWithScriptFields);
            }
            if (std::any_of(script.fieldOverrides.begin(), script.fieldOverrides.end(), [](const auto& field) {
                return std::holds_alternative<ScriptEntityReference>(field.second);
            })) {
                version = kSceneVersionWithEntityReferences;
            }
        }
    }
    out << "SatScene " << version << '\n' << scene.entityCount() << '\n';
    writePhysicsSettings(out, scene.physicsSettings);
    writeAudioSettings(out, scene.audioSettings);
    const auto& ambient = scene.lightingSettings;
    out << ambient.ambientColor[0] << ' ' << ambient.ambientColor[1] << ' '
        << ambient.ambientColor[2] << ' ' << ambient.ambientIntensity << '\n';
    out << std::quoted(ambient.environmentPath) << ' ' << ambient.environmentIntensity << ' '
        << ambient.directionalShadows << ' ' << ambient.shadowResolution << ' ' << ambient.shadowDistance << ' '
        << ambient.shadowCascadeCount << ' ' << ambient.shadowSplitLambda << '\n';
    const auto& bloom = ambient.bloom;
    out << bloom.enabled << ' ' << bloom.threshold << ' ' << bloom.softKnee << ' '
        << bloom.intensity << ' ' << bloom.radius << ' ' << bloom.tint[0] << ' '
        << bloom.tint[1] << ' ' << bloom.tint[2] << ' ' << bloom.highlightClamp << ' ' << bloom.quality << '\n';
    for (const auto& entity : scene.entities()) {
        const auto& t = entity.transform;
        const auto& c = entity.camera;
        out << std::quoted(entity.name) << '\n'
            << std::setprecision(std::numeric_limits<double>::max_digits10)
            << t.position.x << ' ' << t.position.y << ' ' << t.position.z << ' '
            << std::setprecision(std::numeric_limits<float>::max_digits10)
            << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
            << t.scale.x << ' ' << t.scale.y << ' ' << t.scale.z << '\n'
            << entity.hasMesh << ' ' << static_cast<int>(entity.mesh.primitive) << ' '
            << std::quoted(entity.mesh.meshPath) << ' ' << std::quoted(entity.mesh.materialPath) << '\n'
            << entity.hasCamera << ' ' << c.isMain << ' ' << c.fieldOfView << ' '
            << c.nearClip << ' ' << c.farClip;
        for (float channel : c.clearColor) {
            out << ' ' << channel;
        }
        out << '\n' << entity.scripts.size() << '\n';
        for (const auto& script : entity.scripts) {
            out << script.enabled << ' ' << std::quoted(script.path) << '\n';
            if (version >= kSceneVersionWithScriptFields) {
                writeScriptFields(out, script.fieldOverrides, scene);
            }
        }
        writePhysicsComponents(out, entity);
        writeAudioComponents(out, entity);
        const auto& light = entity.light;
        out << entity.hasLight << ' ' << static_cast<int>(light.type) << ' '
            << light.color[0] << ' ' << light.color[1] << ' ' << light.color[2] << ' '
            << light.intensity << ' ' << light.range << ' ' << light.innerConeDegrees << ' '
            << light.outerConeDegrees << '\n';
        if (version >= kSceneVersionWithUi) {
            out << entity.gameUi.has_value();
            if (entity.gameUi) {
                out << ' ' << entity.gameUi->visible << ' ' << std::quoted(entity.gameUi->documentPath)
                    << ' ' << std::quoted(entity.gameUi->fontPath);
            }
            out << '\n';
        }
    }
    // File-local ordinals keep hierarchy links independent of runtime IDs.
    for (const auto& entity : scene.entities()) {
        const auto parent = scene.entityIndex(entity.parentId());
        out << (parent ? *parent + 1 : 0) << '\n';
    }
    return out.str();
}

bool deserializeScene(std::string_view data, Scene& scene, std::string& error) {
    error.clear();
    const auto fail = [&](const std::string& message) { error = message; return false; };
    if (data.size() > kMaxSceneBytes) {
        return fail("Scene exceeds the 16 MiB limit.");
    }
    std::istringstream in{std::string(data)};
    in.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    std::size_t count = 0;
    if (!(in >> magic >> version >> count) || magic != "SatScene" || (version < 1 || version > kSceneVersionWithEntityReferences) || count > kMaxEntities) {
        return fail("Invalid scene header, version or entity count.");
    }
    Scene candidate;
    candidate.clear();
    if (version >= 3 && !readPhysicsSettings(in, candidate.physicsSettings)) {
        return fail("Invalid physics settings.");
    }
    if (version >= 4 && !readAudioSettings(in, candidate.audioSettings, version >= 5)) {
        return fail("Invalid audio settings.");
    }
    const auto readColor = [&](float (&color)[3]) {
        for (float& value : color) {
            if (!(in >> value) || !std::isfinite(value) || value < 0 || value > 1) { return false; }
        }
        return true;
    };
    auto& ambient = candidate.lightingSettings;
    if (version >= 6 && (!readColor(ambient.ambientColor) || !(in >> ambient.ambientIntensity) ||
        !std::isfinite(ambient.ambientIntensity) || ambient.ambientIntensity < 0)) {
        return fail("Invalid ambient lighting settings.");
    }
    if (version >= 7 && (!(in >> std::quoted(ambient.environmentPath) >> ambient.environmentIntensity
        >> ambient.directionalShadows) ||
        (!ambient.environmentPath.empty() && !isTextureAssetPath(ambient.environmentPath)) ||
        !std::isfinite(ambient.environmentIntensity) || ambient.environmentIntensity < 0)) {
        return fail("Invalid environment lighting settings.");
    }
    const auto readRange = [&](float& value, float minimum, float maximum) {
        return (in >> value) && std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if (version >= 8 && (!(in >> ambient.shadowResolution) ||
        (ambient.shadowResolution != 512 && ambient.shadowResolution != 1024 &&
         ambient.shadowResolution != 2048 && ambient.shadowResolution != 4096))) {
        return fail("Invalid directional shadow resolution.");
    }
    if (version >= 10) {
        std::string cascadeFields;
        std::getline(in, cascadeFields);
        std::istringstream cascades{std::move(cascadeFields)};
        cascades.imbue(in.getloc());
        if (!(cascades >> ambient.shadowDistance >> ambient.shadowCascadeCount >> ambient.shadowSplitLambda) ||
            !std::isfinite(ambient.shadowDistance) || ambient.shadowDistance < 10 || ambient.shadowDistance > 1000 ||
            ambient.shadowCascadeCount < 1 || ambient.shadowCascadeCount > 4 ||
            !std::isfinite(ambient.shadowSplitLambda) || ambient.shadowSplitLambda < 0 || ambient.shadowSplitLambda > 1 ||
            !(cascades >> std::ws).eof()) {
            return fail("Invalid directional shadow cascade settings.");
        }
    } else if (version >= 8) {
        float radius = 0;
        if (!readRange(radius, 5, 200)) {
            return fail("Invalid directional shadow radius.");
        }
        ambient.shadowDistance = std::clamp(radius * 2.0f, 10.0f, 1000.0f);
    }
    auto& bloom = ambient.bloom;
    if (version >= 9 && (!(in >> bloom.enabled) || !readRange(bloom.threshold, 0, 100) ||
        !readRange(bloom.softKnee, 0, 1) || !readRange(bloom.intensity, 0, 10) ||
        !readRange(bloom.radius, 0, 64) || !readColor(bloom.tint) ||
        !readRange(bloom.highlightClamp, 0, 65504) || !(in >> bloom.quality) ||
        bloom.quality < 0 || bloom.quality > 2)) {
        return fail("Invalid bloom settings.");
    }
    std::size_t droppedTextures = 0;
    bool hasMain = false;
    for (std::size_t index = 0; index < count; ++index) {
        std::string name;
        if (!(in >> std::quoted(name)) || name.empty() || name.size() > 4096) {
            return fail("Invalid entity name at index " + std::to_string(index));
        }
        Entity& e = candidate.createEntity(std::move(name));
        auto& t = e.transform;
        auto& c = e.camera;
        int primitive = 0;
        std::string legacyTexture;
        auto& surfacePath = version >= 6 ? e.mesh.materialPath : legacyTexture;
        if (!(in >> t.position.x >> t.position.y >> t.position.z
                 >> t.rotation.x >> t.rotation.y >> t.rotation.z
                 >> t.scale.x >> t.scale.y >> t.scale.z
                 >> e.hasMesh >> primitive) ||
            (version >= 2 && (!(in >> std::quoted(e.mesh.meshPath) >> std::quoted(surfacePath)) ||
                (!e.mesh.meshPath.empty() && !isMeshAssetPath(e.mesh.meshPath)) ||
                (!surfacePath.empty() && !(version >= 6 ? isMaterialAssetPath(surfacePath) : isTextureAssetPath(surfacePath))))) ||
            !(in >> e.hasCamera >> c.isMain
                 >> c.fieldOfView >> c.nearClip >> c.farClip) ||
            !finite(t.position) || !finite(t.rotation) || !finite(t.scale) || primitive != 0 ||
            !std::isfinite(c.fieldOfView) || !std::isfinite(c.nearClip) || !std::isfinite(c.farClip) ||
            c.fieldOfView <= 0.0f || c.fieldOfView >= 180.0f || c.nearClip <= 0.0f || c.farClip <= c.nearClip) {
            return fail("Invalid components on entity " + e.name);
        }
        for (float& channel : c.clearColor) {
            if (!(in >> channel) || !std::isfinite(channel)) {
                return fail("Invalid camera color on entity " + e.name);
            }
        }
        if (c.isMain && (!e.hasCamera || hasMain)) {
            return fail("Only one camera may be marked as main.");
        }
        hasMain = hasMain || c.isMain;
        std::size_t scripts = 0;
        if (!(in >> scripts) || scripts > kMaxScripts) {
            return fail("Invalid script count on entity " + e.name);
        }
        for (std::size_t script = 0; script < scripts; ++script) {
            bool enabled = true;
            std::string path;
            if (!(in >> enabled >> std::quoted(path)) || !candidate.attachScript(index, path)) {
                return fail("Invalid or duplicate script path on entity " + e.name);
            }
            auto& component = e.scripts.back();
            component.enabled = enabled;
            if (version >= kSceneVersionWithScriptFields) {
                std::size_t fieldCount = 0;
                if (!(in >> fieldCount) || fieldCount > kMaxScriptFields) {
                    return fail("Invalid script field count for " + path + " on entity " + e.name);
                }
                for (std::size_t field = 0; field < fieldCount; ++field) {
                    if (!readScriptField(in, component.fieldOverrides, version, count)) {
                        return fail("Invalid or duplicate script field for " + path + " on entity " + e.name);
                    }
                }
            }
        }
        if (version >= 3 && !readPhysicsComponents(in, e, error)) { return false; }
        if (version >= 4 && !readAudioComponents(in, e, error, version >= 5)) { return false; }
        if (!legacyTexture.empty()) { ++droppedTextures; }
        if (version >= 6) {
            int type = 0;
            auto& light = e.light;
            if (!(in >> e.hasLight >> type) || type < 0 || type > 2 || !readColor(light.color) ||
                !(in >> light.intensity >> light.range >> light.innerConeDegrees >> light.outerConeDegrees) ||
                !std::isfinite(light.intensity) || light.intensity < 0 ||
                !std::isfinite(light.range) || light.range <= 0 ||
                !std::isfinite(light.innerConeDegrees) || !std::isfinite(light.outerConeDegrees) ||
                light.innerConeDegrees < 0 || light.outerConeDegrees < light.innerConeDegrees ||
                light.outerConeDegrees >= 90) {
                return fail("Invalid light on entity " + e.name);
            }
            light.type = static_cast<LightType>(type);
        }
        if (version >= kSceneVersionWithUi) {
            bool hasUi = false;
            if (!(in >> hasUi)) { return fail("Invalid Game UI component on entity " + e.name); }
            if (hasUi) {
                auto& ui = e.gameUi.emplace();
                if (!(in >> ui.visible >> std::quoted(ui.documentPath) >> std::quoted(ui.fontPath)) ||
                    (!ui.documentPath.empty() && !isUiDocumentPath(ui.documentPath)) ||
                    (!ui.fontPath.empty() && !isUiFontPath(ui.fontPath))) {
                    return fail("Invalid Game UI asset path on entity " + e.name);
                }
            }
        }
    }
    if (version >= kSceneVersionWithEntityReferences) {
        for (std::size_t index = 0; index < count; ++index) {
            for (auto& script : candidate.entity(index)->scripts) {
                for (auto& [name, value] : script.fieldOverrides) {
                    if (auto* reference = std::get_if<ScriptEntityReference>(&value);
                        reference && reference->entityId != 0) {
                        reference->entityId = candidate.entity(static_cast<std::size_t>(reference->entityId - 1))->id();
                    }
                }
            }
        }
    }
    if (version >= 11) {
        // Install links from the leaves up so each prospective parent is still
        // a root. Entities in a cycle never reach a child count of zero.
        std::vector<std::size_t> parents(count + 1);
        std::vector<std::size_t> childCounts(count + 1);
        for (std::size_t ordinal = 1; ordinal <= count; ++ordinal) {
            int parent = 0;
            if (!(in >> parent) || parent < 0 || static_cast<std::size_t>(parent) > count) {
                return fail("Invalid parent ordinal for entity " + std::to_string(ordinal));
            }
            parents[ordinal] = static_cast<std::size_t>(parent);
            ++childCounts[parents[ordinal]];
        }
        std::size_t installed = 0;
        for (std::size_t ordinal = 1; ordinal <= count; ++ordinal) {
            auto child = ordinal;
            while (child != 0 && childCounts[child] == 0) {
                const auto parent = parents[child];
                if (parent != 0 &&
                    !candidate.setParent(candidate.entity(child - 1)->id(), candidate.entity(parent - 1)->id())) {
                    return fail("Invalid scene hierarchy.");
                }
                ++installed;
                // Mark this entity processed, including when it is a root.
                ++childCounts[child];
                --childCounts[parent];
                child = parent;
            }
        }
        if (installed != count) {
            return fail("Scene hierarchy contains a parent cycle.");
        }
    }
    in >> std::ws;
    if (!in.eof()) {
        return fail("Unexpected data after the scene.");
    }
    if (droppedTextures != 0) {
        std::cerr << "Scene load warning: discarded " << droppedTextures
            << " legacy texture references. Assign .satmat materials to restore them.\n";
    }
    scene = std::move(candidate);
    return true;
}

bool loadSceneFile(const std::filesystem::path& path, Scene& scene, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Cannot read scene: " + path.string();
        return false;
    }
    const auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(kMaxSceneBytes)) {
        error = "Invalid or oversized scene: " + path.string();
        return false;
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        error = "Failed reading scene: " + path.string();
        return false;
    }
    return deserializeScene(data, scene, error);
}

bool saveSceneFile(const std::filesystem::path& path, const Scene& scene, std::string& error) {
    const std::string data = serializeScene(scene);
    Scene validated;
    if (!deserializeScene(data, validated, error)) {
        return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    std::error_code ec;
    if (!file) {
        error = "Cannot write scene: " + path.string();
    } else {
        std::filesystem::rename(temporary, path, ec);
        if (!ec) {
            error.clear();
            return true;
        }
        error = "Cannot replace scene: " + path.string() + ": " + ec.message();
    }
    std::filesystem::remove(temporary, ec);
    return false;
}
} // namespace sat
