#include "LuaRuntime.hpp"
#include "LuaScriptFields.hpp"
#include "LuaToolingSync.hpp"
#include "Project.hpp"
#include "Scene.hpp"
#include "SceneFile.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
int failures = 0;
void check(bool result, const char* message) {
    if (!result) { std::cerr << message << '\n'; ++failures; }
}
struct TemporaryFolder {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("sat-components-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryFolder() { std::filesystem::create_directories(path); }
    ~TemporaryFolder() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void write(const std::filesystem::path& path, std::string_view source) {
    std::ofstream file(path, std::ios::binary);
    file << source;
    file.close();
    if (!file) { throw std::runtime_error("Cannot write test script"); }
}
void checkExposedFields(sat::Project& project) {
    const auto path = project.root() / "Scripts" / "tuning.lua";
    constexpr std::string_view source = R"lua(
--[=[ Script tuning parameters ]=]
sat.expose {
    speed = 3.5,
    enabled = true,
    count = 1,
    label = "default"
}
local startSpeed = self.speed
function onStart()
    assert(self.speed == startSpeed)
    assert(math.type(self.count) == "integer")
    assert(self.count == 1 or self.count == 9007199254740993)
    sat.setPosition(self.entity, self.speed, self.enabled and 1 or 0, #self.label)
end
function onUpdate()
    sat.setPosition(self.entity, self.speed, self.enabled and 1 or 0, #self.label)
    self.speed = self.speed + 1
end
)lua";
    write(path, source);
    sat::ScriptFields defaults;
    std::string error;
    if (!sat::inspectLuaScriptFields(path, defaults, error)) {
        std::cerr << error << '\n';
        check(false, "Discover defaults without running the script behavior");
        return;
    }
    check(defaults == sat::ScriptFields{
        {"speed", 3.5}, {"enabled", true}, {"count", std::int64_t{1}}, {"label", std::string("default")}},
        "Discover all exposed field types");

    sat::Scene scene;
    scene.clear();
    scene.createEntity("Tuned");
    scene.createEntity("Default");
    check(scene.attachScript(0, "tuning.lua") && scene.attachScript(1, "tuning.lua"), "Attach tunable script twice");
    const sat::ScriptFields overrides{
        {"speed", 9.125}, {"enabled", false}, {"count", std::int64_t{9007199254740993}},
        {"label", std::string("a \"quoted\" \\ label")}, {"oldField", true}};
    scene.entity(0)->scripts[0].fieldOverrides = overrides;
    if (!project.saveScene(scene, error) || !project.loadScene(scene, error)) {
        std::cerr << error << '\n';
        check(false, "Save and reload exposed overrides");
        return;
    }
    check(scene.entity(0)->scripts[0].fieldOverrides == overrides && scene.entity(1)->scripts[0].fieldOverrides.empty(),
          "Overrides round-trip per attachment without losing type or precision");

    sat::LuaRuntime runtime(scene);
    if (!runtime.start(project.root())) {
        std::cerr << runtime.error() << '\n';
        check(false, "Start a project script with exposed fields");
        return;
    }
    auto& first = *scene.entity(0);
    auto& second = *scene.entity(1);
    check(first.transform.position == sat::DVec3{9.125, 0, 18} &&
          second.transform.position == sat::DVec3{3.5, 1, 7}, "Field values are ready for onStart");
    check(runtime.update(0.0f), "Script can write a field");
    auto& script = first.scripts[0];
    auto before = script;
    script.fieldOverrides["enabled"] = true;
    runtime.applyScriptFieldEdits(first.id(), before, script);
    check(runtime.update(0.0f) && first.transform.position == sat::DVec3{10.125, 1, 18},
          "Live edits preserve the script's writes to other fields");

    before = script;
    script.fieldOverrides["speed"] = 3.5;
    runtime.applyScriptFieldEdits(first.id(), before, script);
    check(runtime.update(0.0f) && first.transform.position.x == 3.5 && second.transform.position.x == 5.5,
          "Editing to the default still updates only the selected instance");
    before = script;
    script.fieldOverrides.erase("speed");
    runtime.applyScriptFieldEdits(first.id(), before, script);
    check(runtime.update(0.0f) && first.transform.position.x == 3.5, "Reset restores the running default");
    runtime.stop();

    script.fieldOverrides["speed"] = std::string("stale type");
    const sat::LuaScriptSource embedded[] = {{"tuning.lua", source}};
    check(runtime.start(embedded) && first.transform.position.x == 3.5, "Embedded scripts ignore stale override types");
    runtime.stop();

    write(path, "sat.expose { entity = 42 }");
    check(!sat::inspectLuaScriptFields(path, defaults, error) && !error.empty() && defaults.empty(),
          "Reject reserved names during discovery");
    check(!runtime.start(project.root()), "Reject reserved names during startup");
    write(path, "sat.expose { value = (function() while true do end end)() }");
    check(!sat::inspectLuaScriptFields(path, defaults, error) && error.find("instruction budget") != std::string::npos,
          "Bound field discovery work");
    write(path, "error('should not run or compile') !");
    check(sat::inspectLuaScriptFields(path, defaults, error) && defaults.empty(), "Legacy scripts have no exposed fields");

    script.fieldOverrides["entity"] = true;
    sat::Scene candidate;
    check(!sat::deserializeScene(sat::serializeScene(scene), candidate, error), "Reject invalid persisted field names");
}

void checkEntityReference(const sat::Project& project) {
    const auto path = project.root() / "Scripts" / "reference.lua";
    write(path, R"lua(
sat.expose { target = sat.entityField() }
function onStart()
    onUpdate()
end
function onUpdate()
    local x = 0
    if self.target and sat.isEntityValid(self.target) then
        x = sat.getPosition(self.target)
    end
    sat.setPosition(self.entity, x, 0, 0)
end
)lua");
    sat::LuaScriptFieldPreview preview;
    preview.refresh(path);
    check(preview.error().empty() && preview.defaults() == sat::ScriptFields{{"target", sat::ScriptEntityReference{}}},
          "Discover the entity field marker");

    sat::Scene scene;
    scene.clear();
    scene.createEntity("Owner");
    scene.createEntity("Target").transform.position.x = 10;
    scene.createEntity("Other").transform.position.x = 20;
    check(scene.attachScript(0, "reference.lua"), "Attach entity reference script");
    const auto originalTarget = scene.entity(1)->id();
    scene.entity(0)->scripts[0].fieldOverrides["target"] = sat::ScriptEntityReference{originalTarget};

    std::string error;
    const auto saved = sat::serializeScene(scene);
    if (!sat::deserializeScene(saved, scene, error)) {
        check(false, "Load a scene containing an entity reference");
        return;
    }
    auto& owner = *scene.entity(0);
    auto& script = owner.scripts[0];
    const auto targetId = scene.entity(1)->id();
    const auto otherId = scene.entity(2)->id();
    check(targetId != originalTarget &&
          script.fieldOverrides.at("target") == sat::ScriptFieldValue{sat::ScriptEntityReference{targetId}},
          "Scene loading remaps an entity reference to the new ID");

    sat::LuaRuntime runtime(scene);
    check(runtime.start(project.root()) && owner.transform.position.x == 10,
          "An assigned reference is an entity ID at startup");
    auto before = script;
    script.fieldOverrides["target"] = sat::ScriptEntityReference{otherId};
    runtime.applyScriptFieldEdits(owner.id(), before, script);
    check(runtime.update(0.0f) && owner.transform.position.x == 20, "Reassign a reference during Play");
    before = script;
    script.fieldOverrides.erase("target");
    runtime.applyScriptFieldEdits(owner.id(), before, script);
    check(runtime.update(0.0f) && owner.transform.position.x == 0, "Reset clears the live reference");
    runtime.stop();

    script.fieldOverrides["target"] = sat::ScriptEntityReference{targetId};
    check(scene.destroyEntity(targetId), "Delete the referenced entity");
    check(sat::deserializeScene(sat::serializeScene(scene), scene, error) &&
          scene.entity(0)->scripts[0].fieldOverrides.at("target") ==
              sat::ScriptFieldValue{sat::ScriptEntityReference{}},
          "A deleted reference never points to the entity that takes its place");
}

void checkProjectDefinitionsSync(const TemporaryFolder& temporary, const sat::Project& project) {
    const auto source = temporary.path / "EngineDefs";
    const auto destination = project.root() / "LuaDefs";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);
    write(source / "sat.lua", "function sat.entityField() end\n");
    write(source / "globals.lua", "self = {}\n");
    write(destination / "sat.lua", "function sat.expose() end\n");
    write(destination / "custom.lua", "-- Project annotations\n");

    check(sat::syncProjectLuaTooling(project.root(), source), "Sync definitions on project open");
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    check(read(destination / "sat.lua") == read(source / "sat.lua") &&
          read(destination / "globals.lua") == read(source / "globals.lua"),
          "Replace stale engine definitions in an existing project");
    check(read(destination / "custom.lua") == "-- Project annotations\n", "Keep project Lua definitions");
    check(read(project.root() / ".luarc.json").find("\"LuaDefs\"") != std::string::npos,
          "Configure LuaLS to load the refreshed definitions");
    const auto settings = read(project.root() / ".vscode" / "settings.json");
    check(settings.find("${workspaceFolder}/LuaDefs") != std::string::npos &&
          settings.find("${workspaceFolder}/selene.toml") != std::string::npos,
          "Configure LuaLS and Selene when opening the project folder directly");
    check(!std::filesystem::exists(project.root() / "SatEngine.code-workspace"),
          "Skip generating a workspace file for a project folder");

    const auto oldTime = std::filesystem::last_write_time(destination / "sat.lua");
    check(sat::syncProjectLuaDefinitions(project.root(), source) &&
          std::filesystem::last_write_time(destination / "sat.lua") == oldTime,
          "Opening a script skips rewriting current definitions");
    write(source / "sat.lua", "function sat.entityField() end\nfunction sat.expose() end\n");
    check(sat::syncProjectLuaDefinitions(project.root(), source) &&
          read(destination / "sat.lua") == read(source / "sat.lua"),
          "Opening a script picks up changes to engine definitions after project open");
}

void checkPreviewReload(const sat::Project& project) {
    const auto path = project.root() / "Scripts" / "reload.lua";
    sat::LuaScriptFieldPreview preview;
    write(path, "sat.expose { speed = 30.0 }");
    const auto timestamp = std::filesystem::last_write_time(path);
    preview.refresh(path);
    check(preview.error().empty() && preview.defaults() == sat::ScriptFields{{"speed", 30.0}},
          "Read the initial Inspector fields");

    write(path, "sat.expose { speed = 60.0 }");
    std::filesystem::last_write_time(path, timestamp);
    preview.refresh(path);
    check(preview.error().empty() && preview.defaults() == sat::ScriptFields{{"speed", 60.0}},
          "Refresh after a same-size save with the same timestamp");

    write(path, "sat.expose { speed = ");
    preview.refresh(path);
    check(!preview.error().empty() && preview.defaults() == sat::ScriptFields{{"speed", 60.0}},
          "An invalid save keeps the last valid preview");

    write(path, "sat.expose { spinning = true }");
    preview.refresh(path);
    check(preview.error().empty() && preview.defaults() == sat::ScriptFields{{"spinning", true}},
          "A corrected save clears the error and replaces the field list");

    std::filesystem::remove(path);
    preview.refresh(path);
    check(!preview.error().empty() && preview.defaults() == sat::ScriptFields{{"spinning", true}},
          "A missing file keeps the last valid preview");

    write(path, "function onUpdate() end");
    preview.refresh(path);
    check(preview.error().empty() && preview.defaults().empty(), "Refresh when a declaration is removed");

    write(path, R"lua(
sat.expose({
    speed = (function() local x, y = 10, 20; return x + y end)(),
    -- field-shaped text and punctuation inside a value are ignored
    label = [=[ignored = true, }]=];
    ["spin" .. "ning"] = true,
    hidden = nil,
})
)lua");
    preview.refresh(path);
    check(preview.error().empty() &&
          preview.fieldOrder() == std::vector<std::string>{"speed", "label", "spinning"},
          "Preview follows declaration order with comments, expressions and computed keys");

    write(path, "sat.expose { spinning = true, label = 'ignored = true, }', speed = 30 }");
    preview.refresh(path);
    check(preview.error().empty() &&
          preview.fieldOrder() == std::vector<std::string>{"spinning", "label", "speed"},
          "Preview refreshes when fields are reordered");
}
} // namespace

int main() {
    TemporaryFolder temporary;
    sat::Project project;
    if (!project.create(temporary.path, "Game")) {
        std::cerr << "Cannot create test project\n";
        return 1;
    }
    checkExposedFields(project);
    checkEntityReference(project);
    checkProjectDefinitionsSync(temporary, project);
    checkPreviewReload(project);
    return failures == 0 ? 0 : 1;
}
