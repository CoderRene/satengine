#include "SceneFile.hpp"

#include "Scene.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

namespace sat {
namespace {
constexpr std::size_t kMaxSceneBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxEntities = 100000;
constexpr std::size_t kMaxScripts = 128;

bool finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const DVec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
} // namespace

std::string serializeScene(const Scene& scene) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "SatScene 1\n" << scene.entities().size() << '\n';
    for (const auto& entity : scene.entities()) {
        const auto& t = entity.transform;
        const auto& c = entity.camera;
        out << std::quoted(entity.name) << '\n'
            << std::setprecision(std::numeric_limits<double>::max_digits10)
            << t.position.x << ' ' << t.position.y << ' ' << t.position.z << ' '
            << std::setprecision(std::numeric_limits<float>::max_digits10)
            << t.rotation.x << ' ' << t.rotation.y << ' ' << t.rotation.z << ' '
            << t.scale.x << ' ' << t.scale.y << ' ' << t.scale.z << '\n'
            << entity.hasMesh << ' ' << static_cast<int>(entity.mesh.primitive) << '\n'
            << entity.hasCamera << ' ' << c.isMain << ' ' << c.fieldOfView << ' '
            << c.nearClip << ' ' << c.farClip;
        for (float channel : c.clearColor) {
            out << ' ' << channel;
        }
        out << '\n' << entity.scripts.size() << '\n';
        for (const auto& script : entity.scripts) {
            out << script.enabled << ' ' << std::quoted(script.path) << '\n';
        }
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
    if (!(in >> magic >> version >> count) || magic != "SatScene" || version != 1 || count > kMaxEntities) {
        return fail("Invalid scene header, version or entity count.");
    }
    Scene candidate;
    candidate.clear();
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
        if (!(in >> t.position.x >> t.position.y >> t.position.z
                 >> t.rotation.x >> t.rotation.y >> t.rotation.z
                 >> t.scale.x >> t.scale.y >> t.scale.z
                 >> e.hasMesh >> primitive >> e.hasCamera >> c.isMain
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
            e.scripts.back().enabled = enabled;
        }
    }
    in >> std::ws;
    if (!in.eof()) {
        return fail("Unexpected data after the scene.");
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
