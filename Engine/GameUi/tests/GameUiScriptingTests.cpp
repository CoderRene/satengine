#include "GameUi.hpp"
#include "Input.hpp"
#include "LuaRuntime.hpp"
#include "Scene.hpp"
#include "SceneFile.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

namespace {

class TestRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        return vertices.empty() || indices.empty() ? 0 : 1;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle texture) override {
        if (texture == 1) { drewFont = true; }
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& path) override {
        Rml::String bytes;
        if (!Rml::GetFileInterface()->LoadFile(path, bytes) || bytes != "test image") { return 0; }
        dimensions = {16, 16};
        readImage = true;
        return 2;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i) override {
        return pixels.empty() ? 0 : 1;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}

    bool drewFont = false;
    bool readImage = false;
};

constexpr std::string_view kRml = R"(
<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body>
    <button id="action" value="clicked"><span id="label">Ready</span></button>
    <input id="field" type="text" />
    <img src="indicator.png" />
</body>
</rml>
)";

constexpr std::string_view kRcss = R"(
body { font-family: "JetBrains Mono"; font-size: 16px; }
button { position: absolute; left: 40px; top: 30px; width: 140px; height: 32px; }
input { position: absolute; left: 40px; top: 75px; width: 140px; height: 32px; }
img { position: absolute; left: 200px; top: 30px; }
)";

constexpr std::string_view kScript = R"(
local ui = sat.ui
local clicks = 0
local updates = 0

function onStart()
    assert(ui.loadFont("Assets/UI/font.ttf"))
    local missing, err = ui.load("Assets/UI/missing.rml")
    assert(missing == nil and err:find("missing.rml", 1, true))
    doc = assert(ui.load("Assets/UI/hud.rml"))
    action = assert(ui.element(doc, "action"))
    label = assert(ui.element(doc, "label"))
    field = assert(ui.element(doc, "field"))
    assert(ui.setText(label, "<img src='unexpected.png'/>"))
    assert(ui.setText(label, "<img src='unexpected.png'/>"))
    listener = assert(ui.on(action, "click", function(event)
        assert(event.type == "click" and event.x == 65 and event.y == 45)
        assert(event.key == 0 and event.value == "clicked")
        assert(ui.wantsMouse())
        assert(ui.setText(label, "Clicked"))
        clicks = clicks + 1
        assert(ui.off(listener))
    end))
    assert(ui.show(doc))
end

function onUpdate()
    updates = updates + 1
    assert(clicks == 1)
    if updates == 2 then
        assert(ui.wantsKeyboard())
        assert(ui.getValue(field) == "é")
        assert(ui.setRml(action, "<span id='label'>New</span>"))
        assert(not ui.setText(label, "stale"))
        local replacementLabel = assert(ui.element(doc, "label"))
        assert(ui.close(doc))
        local replacementDoc = assert(ui.load("Assets/UI/hud.rml"))
        assert(not ui.show(doc) and not ui.setText(replacementLabel, "stale"))
        assert(ui.getValue(field) == nil and ui.element(doc, "label") == nil)
        assert(ui.close(replacementDoc))
    end
end
)";

constexpr std::string_view kOtherScript = R"(
function onStart()
    assert(doc == nil and clicks == nil)
end
function onUpdate()
    assert(doc == nil and clicks == nil)
end
)";

bool modelsAndReload(std::string_view font) {
    using namespace std::chrono_literals;
    constexpr std::string_view path = "Assets/UI/model.rml";
    std::string stylesheet = "body { font-family: JetBrains Mono; } #count { font-size: 16px; }";
    std::string source = R"(<rml><head><link type="text/rcss" href="theme.rcss"/></head><body><div data-model="hud">
        <button id="action" style="position:absolute; left:10px; top:10px; width:100px; height:36px;">Add</button>
        <input id="name" type="text" data-value="name" style="position:absolute; left:10px; top:60px; width:100px; height:36px;"/>
        <span id="count">{{count}}</span><span id="removed"/>
    </div></body></rml>)";
    TestRenderer renderer;
    sat::GameUi ui(static_cast<Rml::RenderInterface*>(&renderer),
        [&](std::string_view requested, std::string& bytes, std::string&) {
            if (requested == path) { bytes = source; }
            else if (requested == "Assets/UI/theme.rcss") { bytes = stylesheet; }
            else if (requested == "Assets/UI/font.ttf") { bytes = font; }
            else { return sat::AssetReadResult::Missing; }
            return sat::AssetReadResult::Found;
        });
    ui.enableHotReload();
    ui.setViewport(400, 300);

    sat::Scene scene;
    scene.clear();
    scene.createEntity("HUD").gameUi = sat::GameUiComponent{std::string(path), "Assets/UI/font.ttf", true};
    if (!scene.attachScript(0, "model.lua")) { return false; }
    const auto file = sat::serializeScene(scene);
    sat::Scene loaded;
    std::string error;
    if (!sat::deserializeScene(file, loaded, error) || !loaded.entity(0) ||
        loaded.entity(0)->gameUi != scene.entity(0)->gameUi) {
        std::cerr << "Game UI component round trip failed " << error << '\n';
        return false;
    }
    constexpr std::string_view script = R"(
local ui = sat.ui
local frames = 0
function onStart()
    assert(ui.createModel("hud", {count = 4, name = ""}))
    assert(not ui.createModel("hud", {count = 0}))
    assert(not ui.createModel("invalid", {["1illegal"] = true}))
    doc = assert(ui.getDocument(self.entity))
    action = assert(ui.element(doc, "action"))
    removed = assert(ui.element(doc, "removed"))
    assert(ui.on(action, "click", function()
        assert(ui.setModelValue("hud", "count", ui.getModelValue("hud", "count") + 1))
    end))
end
function onUpdate()
    frames = frames + 1
    assert(ui.getModelValue("hud", "count") == math.min(frames, 2) + 4)
    if frames >= 2 then
        assert(not ui.setText(removed, "stale"))
        assert(ui.setClass(action, "clicked", true))
    end
    if frames >= 3 then assert(ui.getModelValue("hud", "name") == "é") end
end
)";
    const sat::LuaScriptSource scripts[]{{"model.lua", script}};
    sat::LuaRuntime runtime(loaded);
    runtime.setGameUi(&ui);
    if (!runtime.start(scripts)) { std::cerr << runtime.error() << '\n'; return false; }
    const auto click = [&](double x, double y) {
        sat::Input input;
        input.setCursorPosition(x, y);
        input.setMouseButton(sat::MouseButton::Left, true);
        input.setMouseButton(sat::MouseButton::Left, false);
        ui.processInput(input, 0, 0);
    };
    const auto checkText = [&](std::string_view expected) {
        auto* context = Rml::GetContext(0);
        if (!context || context->GetNumDocuments() != 1) { return false; }
        auto* element = context->GetDocument(0)->GetElementById("count");
        return element && element->GetInnerRML() == expected;
    };
    ui.update();
    click(20, 20);
    if (!runtime.update(0.01f)) { std::cerr << runtime.error() << '\n'; return false; }
    ui.update();
    if (!checkText("5")) { return false; }

    auto* context = Rml::GetContext(0);
    auto* document = context->GetDocument(0);
    auto* counter = document->GetElementById("count");
    if (counter->GetProperty<float>("font-size") != 16.0f) { return false; }
    auto* field = document->GetElementById("name");
    if (!field->Focus()) { return false; }

    stylesheet = "body { font-family: JetBrains Mono; } #count { font-size: 24px; }";
    std::this_thread::sleep_for(330ms);
    ui.update();
    if (context->GetDocument(0) != document || document->GetElementById("count") != counter ||
        counter->GetProperty<float>("font-size") != 24.0f || context->GetFocusElement() != field ||
        !checkText("5")) {
        std::cerr << "Stylesheet reload did not preserve the live document and apply the new rules\n";
        return false;
    }

    const auto position = source.find("<span id=\"removed\"/>");
    source.erase(position, std::string_view("<span id=\"removed\"/>").size());
    source.replace(source.find("{{count}}"), 9, "Reloaded {{count}}");
    std::this_thread::sleep_for(330ms);
    ui.update();
    if (!checkText("Reloaded 5")) { return false; }
    click(20, 20);
    if (!runtime.update(0.01f)) { std::cerr << runtime.error() << '\n'; return false; }
    ui.update();
    if (!checkText("Reloaded 6")) { return false; }

    const auto oldSource = source;
    source = "<rml><head></head><body><span>Broken</div></body></rml>";
    std::this_thread::sleep_for(330ms);
    ui.update();
    if (!checkText("Reloaded 6")) { return false; }
    source = oldSource;
    click(20, 70);
    sat::Input input;
    input.setCursorPosition(20, 70);
    input.addText(U'é');
    ui.processInput(input, 0, 0);
    if (!runtime.update(0.01f)) { std::cerr << runtime.error() << '\n'; return false; }
    runtime.stop();
    return !ui.modelValue("hud", "count");
}

} // namespace

int testBindings(std::string_view font) {
    int stylesheetReads = 0;
    int unexpectedImageReads = 0;
    TestRenderer renderer;
    sat::GameUi ui(static_cast<Rml::RenderInterface*>(&renderer),
        [&](std::string_view path, std::string& bytes, std::string&) {
            if (path == "Assets/UI/font.ttf") {
                bytes = font;
            } else if (path == "Assets/UI/hud.rml") {
                bytes = kRml;
            } else if (path == "Assets/UI/hud.rcss") {
                bytes = kRcss;
                ++stylesheetReads;
            } else if (path == "Assets/UI/indicator.png") {
                bytes = "test image";
            } else {
                if (path.ends_with("unexpected.png")) { ++unexpectedImageReads; }
                return sat::AssetReadResult::Missing;
            }
            return sat::AssetReadResult::Found;
        });

    sat::Scene scene;
    scene.clear();
    scene.createEntity("HUD");
    if (!scene.attachScript(0, "hud.lua") || !scene.attachScript(0, "other.lua")) { return 1; }
    const sat::LuaScriptSource sources[]{{"hud.lua", kScript}, {"other.lua", kOtherScript}};
    sat::Input input;
    sat::LuaRuntime runtime(scene, &input);
    runtime.setGameUi(&ui);
    ui.setViewport(400, 300);

    const auto click = [&](double x, double y) {
        input.beginFrame();
        input.setCursorPosition(x + 20, y + 10);
        input.setMouseButton(sat::MouseButton::Left, true);
        input.setMouseButton(sat::MouseButton::Left, false);
        ui.processInput(input, 20, 10);
    };

    if (!runtime.start(sources)) {
        std::cerr << runtime.error() << '\n';
        return 1;
    }
    ui.update();
    ui.render();
    if (!renderer.drewFont || !renderer.readImage || stylesheetReads != 1 || unexpectedImageReads != 0) {
        std::cerr << "HUD did not render the expected font and assets\n";
        return 1;
    }

    click(65, 45);
    if (!runtime.update(1.0f / 60.0f)) {
        std::cerr << runtime.error() << '\n';
        return 1;
    }
    click(65, 45);
    click(65, 90);
    input.addText(U'é');
    ui.processInput(input, 20, 10, 1, 1, false);
    if (!runtime.update(1.0f / 60.0f)) {
        std::cerr << runtime.error() << '\n';
        return 1;
    }
    runtime.stop();
    ui.clear();

    // Stop with a live subscription and an event queued for the retiring VM.
    if (!runtime.start(sources)) {
        std::cerr << runtime.error() << '\n';
        return 1;
    }
    ui.update();
    click(65, 45);
    runtime.stop();
    for (const auto& event : ui.takeEvents()) {
        if (ui.subscribed(event.listener, event.owner)) {
            std::cerr << "A Lua UI listener survived runtime stop\n";
            return 1;
        }
    }
    ui.clear();
    return 0;
}


int main() {
    std::ifstream fontFile(SAT_GAME_UI_TEST_FONT, std::ios::binary);
    const std::string font{std::istreambuf_iterator<char>(fontFile), std::istreambuf_iterator<char>()};
    if (font.empty()) {
        std::cerr << "Cannot read the Game UI test font\n";
        return 1;
    }
    if (testBindings(font) != 0) { return 1; }
    if (!modelsAndReload(font)) {
        std::cerr << "Game UI model or hot reload test failed\n";
        return 1;
    }
    return 0;
}
