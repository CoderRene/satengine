#include "GameUi.hpp"

#include "Input.hpp"
#include "Scene.hpp"
#include "module/UiEventQueue.hpp"
#include "module/UiFileInterface.hpp"
#include "module/UiSystemInterface.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sat {
namespace {

struct UiLibrary {
    UiSystemInterface system;
    UiFileInterface files;
    std::size_t users = 0;
    std::uint64_t nextContext = 0;
};

UiLibrary& library() {
    static UiLibrary instance;
    return instance;
}

class LibraryLease {
public:
    LibraryLease() {
        auto& rml = library();
        if (rml.users == 0) {
            Rml::SetSystemInterface(&rml.system);
            Rml::SetFileInterface(&rml.files);
            if (!Rml::Initialise()) {
                std::clog << "Game UI: RmlUi initialization failed\n";
                Rml::SetFileInterface(nullptr);
                Rml::SetSystemInterface(nullptr);
                return;
            }
        }
        ++rml.users;
        acquired = true;
    }
    ~LibraryLease() {
        if (acquired && --library().users == 0) { Rml::Shutdown(); }
    }
    LibraryLease(const LibraryLease&) = delete;
    LibraryLease& operator=(const LibraryLease&) = delete;
    bool acquired = false;
};

template<class Value>
struct Slot {
    Value value{};
    std::uint32_t generation = 0;

    void assign(Value replacement) {
        value = std::move(replacement);
        if (++generation == 0) { ++generation; }
    }
};

template<class Handle, class Value>
Slot<Value>* findSlot(std::vector<Slot<Value>>& slots, Handle handle) {
    if (!handle.generation || handle.index >= slots.size()) { return nullptr; }
    auto& slot = slots[handle.index];
    return slot.generation == handle.generation ? &slot : nullptr;
}

template<class Handle, class Value>
Handle insertSlot(std::vector<Slot<Value>>& slots, Value value) {
    auto it = std::find_if(slots.begin(), slots.end(), [](const auto& slot) { return !slot.value; });
    const auto index = static_cast<std::uint32_t>(it - slots.begin());
    if (it == slots.end()) { slots.emplace_back(); }
    slots[index].assign(std::move(value));
    return {index, slots[index].generation};
}

constexpr auto kKeys = [] {
    using namespace Rml::Input;
    std::array<KeyIdentifier, static_cast<std::size_t>(Key::Menu) + 1> keys{};
    const auto range = [&](Key first, Key last, KeyIdentifier start) {
        for (int key = static_cast<int>(first); key <= static_cast<int>(last); ++key) {
            keys[key] = static_cast<KeyIdentifier>(start + key - static_cast<int>(first));
        }
    };
    range(Key::A, Key::Z, KI_A);
    range(Key::Digit0, Key::Digit9, KI_0);
    range(Key::F1, Key::F24, KI_F1);
    range(Key::Keypad0, Key::Keypad9, KI_NUMPAD0);
    const std::pair<Key, KeyIdentifier> other[] = {
        {Key::Space, KI_SPACE}, {Key::Apostrophe, KI_OEM_7}, {Key::Comma, KI_OEM_COMMA},
        {Key::Minus, KI_OEM_MINUS}, {Key::Period, KI_OEM_PERIOD}, {Key::Slash, KI_OEM_2},
        {Key::Semicolon, KI_OEM_1}, {Key::Equal, KI_OEM_PLUS}, {Key::LeftBracket, KI_OEM_4},
        {Key::Backslash, KI_OEM_5}, {Key::RightBracket, KI_OEM_6}, {Key::GraveAccent, KI_OEM_3},
        {Key::Escape, KI_ESCAPE}, {Key::Enter, KI_RETURN}, {Key::Tab, KI_TAB},
        {Key::Backspace, KI_BACK}, {Key::Insert, KI_INSERT}, {Key::Delete, KI_DELETE},
        {Key::Right, KI_RIGHT}, {Key::Left, KI_LEFT}, {Key::Down, KI_DOWN}, {Key::Up, KI_UP},
        {Key::PageUp, KI_PRIOR}, {Key::PageDown, KI_NEXT}, {Key::Home, KI_HOME}, {Key::End, KI_END},
        {Key::CapsLock, KI_CAPITAL}, {Key::ScrollLock, KI_SCROLL}, {Key::NumLock, KI_NUMLOCK},
        {Key::PrintScreen, KI_SNAPSHOT}, {Key::Pause, KI_PAUSE}, {Key::KeypadDecimal, KI_DECIMAL},
        {Key::KeypadDivide, KI_DIVIDE}, {Key::KeypadMultiply, KI_MULTIPLY}, {Key::KeypadSubtract, KI_SUBTRACT},
        {Key::KeypadAdd, KI_ADD}, {Key::KeypadEnter, KI_NUMPADENTER}, {Key::KeypadEqual, KI_OEM_NEC_EQUAL},
        {Key::LeftShift, KI_LSHIFT}, {Key::LeftControl, KI_LCONTROL}, {Key::LeftAlt, KI_LMENU},
        {Key::LeftSuper, KI_LWIN}, {Key::RightShift, KI_RSHIFT}, {Key::RightControl, KI_RCONTROL},
        {Key::RightAlt, KI_RMENU}, {Key::RightSuper, KI_RWIN}, {Key::Menu, KI_APPS}
    };
    for (const auto& [key, mapped] : other) { keys[static_cast<std::size_t>(key)] = mapped; }
    return keys;
}();

int modifiers(const Input& input) {
    using namespace Rml::Input;
    int result = 0;
    if (input.keyHeld(Key::LeftShift) || input.keyHeld(Key::RightShift)) { result |= KM_SHIFT; }
    if (input.keyHeld(Key::LeftControl) || input.keyHeld(Key::RightControl)) { result |= KM_CTRL; }
    if (input.keyHeld(Key::LeftAlt) || input.keyHeld(Key::RightAlt)) { result |= KM_ALT; }
    if (input.keyHeld(Key::LeftSuper) || input.keyHeld(Key::RightSuper)) { result |= KM_META; }
    return result;
}

} // namespace

namespace {

struct UiDocumentState {
    Rml::ElementDocument* node = nullptr;
    std::string path;
    std::string source;
    std::optional<std::string> rejectedSource;
    UiFileInterface::StyleSheets styleSheets;
    explicit operator bool() const { return node != nullptr; }
};

struct UiModelState {
    UiOwner owner;
    Rml::DataModelHandle handle;
    std::unordered_map<std::string, UiDataValue> fields;
};

Rml::ElementDocument* parseUiDocument(Rml::Context& context, const std::string& path, const std::string& source) {
    if (source.find_first_not_of(" \t\r\n") == std::string::npos) { return nullptr; }
    const auto errors = library().system.errors();
    auto* document = context.LoadDocumentFromMemory(source, path);
    if (document && errors != library().system.errors()) {
        document->Close();
        return nullptr;
    }
    return document;
}

bool styleSheetsChanged(const UiFileInterface::StyleSheets& styleSheets) {
    std::string current;
    bool changed = false;
    for (const auto& [path, previous] : styleSheets) {
        current.clear();
        if (library().files.LoadFile(path, current)) {
            changed |= !previous || current != *previous;
        } else if (previous) {
            // Keep the active rules while an editor replaces a file on save.
            return false;
        }
    }
    return changed;
}

void clearMarkupCaches() {
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
}

} // namespace

struct GameUi::Impl {
    struct Element {
        Rml::ObserverPtr<Rml::Element> node;
        std::optional<std::string> text;
        explicit operator bool() const { return node.get() != nullptr; }
    };

    Impl(void* interface, AssetReader assetReader) : reader(std::move(assetReader)) {
        if (!rml.acquired || !interface) { return; }
        name = "SatGameUi" + std::to_string(library().nextContext++);
        context = Rml::CreateContext(name, {1, 1}, static_cast<Rml::RenderInterface*>(interface));
        if (!context) { std::clog << "Game UI: failed to create a context\n"; }
    }

    ~Impl() {
        if (context) {
            events.clear();
            elements.clear();
            Rml::RemoveContext(name);
            if (library().users > 1) { Rml::ReleaseRenderManagers(); }
        }
    }

    Rml::ElementDocument* document(UiDocument handle) {
        const auto* slot = findSlot(documents, handle);
        return slot ? slot->value.node : nullptr;
    }

    Element* element(UiElement handle) {
        auto* slot = findSlot(elements, handle);
        return slot && slot->value ? &slot->value : nullptr;
    }

    LibraryLease rml;
    AssetReader reader;
    std::string name;
    std::unordered_set<std::string> fonts;
    Rml::Context* context = nullptr;
    std::vector<Slot<UiDocumentState>> documents;
    std::unordered_map<std::string, UiModelState> models;
    struct SceneDocument {
        GameUiComponent component;
        UiDocument handle;
    };
    std::unordered_map<EntityId, SceneDocument> sceneDocuments;
    const Scene* scene = nullptr;
    std::optional<std::uint64_t> sceneRevision;
    std::chrono::steady_clock::time_point nextReload{};
    bool hotReload = false;
    std::vector<Slot<Element>> elements;
    UiEventQueue events;
    std::array<bool, 8> mouseDown{};
    std::array<bool, kKeys.size()> keyDown{};
    bool pointerInside = false;
    bool keyboardActive = false;
    bool mouseInteracting = false;
};

GameUi::GameUi(void* rmlRenderInterface, AssetReader reader)
    : m_impl(std::make_unique<Impl>(rmlRenderInterface, std::move(reader))) {}

GameUi::~GameUi() = default;

bool GameUi::valid() const { return m_impl->context != nullptr; }

void GameUi::setViewport(int width, int height) {
    if (valid() && width > 0 && height > 0) { m_impl->context->SetDimensions({width, height}); }
}

void GameUi::processInput(const Input& input, float offsetX, float offsetY,
    float scaleX, float scaleY, bool pointer, bool keyboard) {
    if (!valid()) { return; }
    auto& impl = *m_impl;
    UiFileInterface::Scope files(impl.reader);
    auto& context = *impl.context;
    const int mods = modifiers(input);
    const auto cursor = input.cursorPosition();
    const auto dimensions = context.GetDimensions();
    const double x = (cursor.x - offsetX) * scaleX;
    const double y = (cursor.y - offsetY) * scaleY;
    pointer = pointer && x >= 0 && y >= 0 && x < dimensions.x && y < dimensions.y;
    if (pointer) {
        impl.mouseInteracting = !context.ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), mods);
    } else if (impl.pointerInside) {
        context.ProcessMouseLeave();
    }
    impl.pointerInside = pointer;
    for (std::size_t index = 0; index < impl.mouseDown.size(); ++index) {
        const auto button = static_cast<MouseButton>(index);
        if (pointer && input.mousePressed(button)) {
            impl.mouseDown[index] = true;
            impl.mouseInteracting |= !context.ProcessMouseButtonDown(static_cast<int>(index), mods);
        }
        if (impl.mouseDown[index] && !input.mouseHeld(button)) {
            impl.mouseDown[index] = false;
            impl.mouseInteracting |= !context.ProcessMouseButtonUp(static_cast<int>(index), mods);
        }
    }
    const auto scroll = input.scrollDelta();
    if (pointer && (scroll.x != 0 || scroll.y != 0)) {
        context.ProcessMouseWheel({-static_cast<float>(scroll.x), -static_cast<float>(scroll.y)}, mods);
    }
    if (!pointer && std::none_of(impl.mouseDown.begin(), impl.mouseDown.end(), [](bool down) { return down; })) {
        impl.mouseInteracting = false;
    }

    impl.keyboardActive = keyboard;
    for (std::size_t index = 0; index < kKeys.size(); ++index) {
        const auto mapped = kKeys[index];
        if (mapped == Rml::Input::KI_UNKNOWN) { continue; }
        const auto key = static_cast<Key>(index);
        if (keyboard && input.keyPressed(key)) {
            impl.keyDown[index] = true;
            context.ProcessKeyDown(mapped, mods);
        }
        if (keyboard && impl.keyDown[index]) {
            for (unsigned repeat = input.keyRepeats(key); repeat > 0; --repeat) {
                context.ProcessKeyDown(mapped, mods);
            }
        }
        if (impl.keyDown[index] && (!keyboard || !input.keyHeld(key))) {
            impl.keyDown[index] = false;
            context.ProcessKeyUp(mapped, mods);
        }
    }
    if (keyboard) {
        for (const char32_t character : input.text()) {
            if (character >= U' ' && character != U'\x7f') { context.ProcessTextInput(static_cast<Rml::Character>(character)); }
        }
    }
}

void GameUi::update() {
    if (valid()) {
        UiFileInterface::Scope files(m_impl->reader);
        auto& impl = *m_impl;
        const auto now = std::chrono::steady_clock::now();
        if (impl.hotReload && now >= impl.nextReload) {
            impl.nextReload = now + std::chrono::milliseconds(300);
            for (auto& slot : impl.documents) {
                auto& document = slot.value;
                std::string source;
                if (!document || !library().files.LoadFile(document.path, source)) { continue; }
                if (source == document.source) {
                    document.rejectedSource.reset();
                    if (styleSheetsChanged(document.styleSheets)) {
                        UiFileInterface::StyleSheets dependencies;
                        UiFileInterface::Scope track(impl.reader, &dependencies);
                        document.node->ReloadStyleSheet();
                        document.styleSheets = std::move(dependencies);
                    }
                    continue;
                }
                if (document.rejectedSource && source == *document.rejectedSource) { continue; }

                clearMarkupCaches();
                UiFileInterface::StyleSheets dependencies;
                UiFileInterface::Scope track(impl.reader, &dependencies);
                auto* replacement = parseUiDocument(*impl.context, document.path, source);
                if (!replacement) {
                    document.rejectedSource = std::move(source);
                    continue;
                }
                document.source = std::move(source);
                document.rejectedSource.reset();
                document.styleSheets = std::move(dependencies);
                auto* previous = document.node;
                const bool visible = previous->IsVisible();
                const auto* focus = impl.context->GetFocusElement();
                const bool focused = focus && focus->GetOwnerDocument() == previous;
                const std::string focusId = focused ? focus->GetId() : std::string{};
                impl.events.rebindDocument(*previous, *replacement);
                for (auto& elementSlot : impl.elements) {
                    auto& element = elementSlot.value;
                    if (!element || element.node->GetOwnerDocument() != previous) { continue; }
                    const auto& id = element.node->GetId();
                    auto* next = id.empty() ? nullptr : replacement->GetElementById(id);
                    element = next ? Impl::Element{next->GetObserverPtr(), {}} : Impl::Element{};
                }
                document.node = replacement;
                previous->Close();
                if (visible) {
                    replacement->Show(Rml::ModalFlag::None, focused ? Rml::FocusFlag::Auto : Rml::FocusFlag::None);
                    if (!focusId.empty()) {
                        if (auto* element = replacement->GetElementById(focusId)) { element->Focus(); }
                    }
                }
            }
        }
        impl.context->Update();
        impl.events.pruneDetached();
    }
}

bool GameUi::hasDocuments() const {
    return valid() && m_impl->context->GetNumDocuments() > 0;
}

void GameUi::render() {
    if (valid()) {
        UiFileInterface::Scope files(m_impl->reader);
        m_impl->context->Render();
    }
}

void GameUi::enableHotReload() { m_impl->hotReload = true; }

bool GameUi::createModel(std::string_view name, std::span<const UiDataField> fields, UiOwner owner) {
    if (!valid() || name.empty() || m_impl->models.contains(std::string(name))) { return false; }
    auto& impl = *m_impl;
    std::string modelName(name);
    auto constructor = impl.context->CreateDataModel(modelName);
    if (!constructor) { return false; }
    struct Rollback {
        Rml::Context* context;
        const std::string& name;
        ~Rollback() { if (context) { context->RemoveDataModel(name); } }
    } rollback{impl.context, modelName};
    auto [position, inserted] = impl.models.try_emplace(modelName, UiModelState{owner, constructor.GetModelHandle(), {}});
    if (!inserted) { return false; }
    try {
        auto& model = position->second;
        for (const auto& [field, value] : fields) {
            auto [entry, unique] = model.fields.try_emplace(field, value);
            if (!unique || !constructor.BindFunc(field, [storage = &entry->second](Rml::Variant& out) {
                    std::visit([&](const auto& stored) { out = Rml::Variant(stored); }, *storage);
                }, [storage = &entry->second, field, handle = model.handle](const Rml::Variant& input) mutable {
                    std::visit([&](auto& stored) {
                        using Value = std::decay_t<decltype(stored)>;
                        auto next = input.Get<Value>();
                        if (stored != next) {
                            stored = std::move(next);
                            handle.DirtyVariable(field);
                        }
                    }, *storage);
                })) {
                impl.models.erase(position);
                return false;
            }
        }
    } catch (...) {
        impl.models.erase(position);
        throw;
    }
    rollback.context = nullptr;
    return true;
}

bool GameUi::setModelValue(std::string_view name, std::string_view field, UiDataValue value) {
    const auto model = m_impl->models.find(std::string(name));
    if (model == m_impl->models.end()) { return false; }
    const auto variable = model->second.fields.find(std::string(field));
    if (variable == model->second.fields.end()) { return false; }
    if (variable->second != value) {
        variable->second = std::move(value);
        model->second.handle.DirtyVariable(variable->first);
    }
    return true;
}

std::optional<UiDataValue> GameUi::modelValue(std::string_view name, std::string_view field) const {
    const auto model = m_impl->models.find(std::string(name));
    if (model == m_impl->models.end()) { return std::nullopt; }
    const auto variable = model->second.fields.find(std::string(field));
    return variable == model->second.fields.end() ? std::nullopt : std::optional{variable->second};
}

void GameUi::removeModels(UiOwner owner) {
    auto& impl = *m_impl;
    std::erase_if(impl.models, [&](const auto& entry) {
        if (entry.second.owner != owner) { return false; }
        impl.context->RemoveDataModel(entry.first);
        return true;
    });
}

std::string GameUi::syncScene(const Scene& scene) {
    auto& impl = *m_impl;
    if (impl.scene != &scene) {
        clearScene();
        impl.scene = &scene;
    }
    if (impl.sceneRevision == scene.editRevision()) { return {}; }
    impl.sceneRevision = scene.editRevision();
    std::erase_if(impl.sceneDocuments, [&](const auto& entry) {
        const auto* entity = scene.entityById(entry.first);
        if (entity && entity->gameUi && entity->gameUi->documentPath == entry.second.component.documentPath &&
            entity->gameUi->fontPath == entry.second.component.fontPath) { return false; }
        close(entry.second.handle);
        return true;
    });
    std::string error;
    for (const auto& entity : scene.entities()) {
        if (!entity.gameUi || entity.gameUi->documentPath.empty()) { continue; }
        if (!valid()) { return "Game UI is unavailable"; }
        const auto& component = *entity.gameUi;
        auto& document = impl.sceneDocuments.try_emplace(entity.id(), Impl::SceneDocument{component, {}}).first->second;
        const bool needsLoad = !document.handle.generation;
        if (needsLoad) {
            if (!component.fontPath.empty() && !loadFont(component.fontPath)) {
                error = entity.name + ": cannot load Game UI font " + component.fontPath;
                continue;
            }
            document.handle = loadDocument(component.documentPath);
            if (!document.handle.generation) {
                error = entity.name + ": cannot load Game UI document " + component.documentPath;
                continue;
            }
        }
        if (needsLoad || document.component.visible != component.visible) {
            if (component.visible) { show(document.handle); }
            else { hide(document.handle); }
            document.component.visible = component.visible;
        }
    }
    return error;
}

UiDocument GameUi::sceneDocument(const Scene& scene, std::uint64_t entity) {
    auto& impl = *m_impl;
    if (impl.scene && impl.scene != &scene) { return {}; }
    impl.scene = &scene;
    const auto found = impl.sceneDocuments.find(entity);
    if (found != impl.sceneDocuments.end() && impl.document(found->second.handle)) { return found->second.handle; }
    // Load this component on demand so its script can create its model first.
    const auto* owner = scene.entityById(entity);
    if (!owner || !owner->gameUi || owner->gameUi->documentPath.empty()) { return {}; }
    const auto& component = *owner->gameUi;
    if (!component.fontPath.empty() && !loadFont(component.fontPath)) { return {}; }
    const auto handle = loadDocument(component.documentPath);
    if (handle.generation) {
        impl.sceneDocuments.insert_or_assign(entity, Impl::SceneDocument{component, handle});
        if (component.visible) { show(handle); }
    }
    return handle;
}

void GameUi::clearScene() {
    auto& impl = *m_impl;
    for (const auto& entry : impl.sceneDocuments) { close(entry.second.handle); }
    impl.sceneDocuments.clear();
    impl.sceneRevision.reset();
    impl.scene = nullptr;
}

bool GameUi::loadFont(std::string_view path) {
    if (!valid()) { return false; }
    UiFileInterface::Scope files(m_impl->reader);
    auto& fonts = m_impl->fonts;
    std::string name(path);
    if (fonts.contains(name)) { return true; }
    if (!Rml::LoadFontFace(name)) { return false; }
    fonts.insert(std::move(name));
    return true;
}

UiDocument GameUi::loadDocument(std::string_view path) {
    if (!valid()) { return {}; }
    UiFileInterface::Scope files(m_impl->reader);
    // These caches are global and keyed by path, which can match across readers.
    if (!m_impl->hotReload) {
        if (library().users > 1) { clearMarkupCaches(); }
        auto* document = m_impl->context->LoadDocument(std::string(path));
        return document ? insertSlot<UiDocument>(m_impl->documents, UiDocumentState{document, {}, {}, {}, {}}) : UiDocument{};
    }
    UiDocumentState state;
    state.path = path;
    if (!library().files.LoadFile(state.path, state.source)) { return {}; }
    // A cached stylesheet would bypass the file interface and evade tracking.
    clearMarkupCaches();
    UiFileInterface::Scope track(m_impl->reader, &state.styleSheets);
    state.node = parseUiDocument(*m_impl->context, state.path, state.source);
    return state ? insertSlot<UiDocument>(m_impl->documents, std::move(state)) : UiDocument{};
}

bool GameUi::show(UiDocument handle) {
    auto* document = m_impl->document(handle);
    if (!document) { return false; }
    UiFileInterface::Scope files(m_impl->reader);
    document->Show();
    return true;
}

bool GameUi::hide(UiDocument handle) {
    auto* document = m_impl->document(handle);
    if (!document) { return false; }
    document->Hide();
    return true;
}

bool GameUi::close(UiDocument handle) {
    auto* document = m_impl->document(handle);
    if (!document) { return false; }
    m_impl->events.unsubscribeDocument(*document);
    for (auto& slot : m_impl->elements) {
        if (slot.value && slot.value.node->GetOwnerDocument() == document) { slot.value = {}; }
    }
    m_impl->documents[handle.index].value = {};
    document->Close();
    return true;
}

UiElement GameUi::element(UiDocument handle, std::string_view id) {
    auto* document = m_impl->document(handle);
    auto* element = document && !id.empty() ? document->GetElementById(std::string(id)) : nullptr;
    if (!element) { return {}; }
    for (std::size_t i = 0; i < m_impl->elements.size(); ++i) {
        const auto& slot = m_impl->elements[i];
        if (slot.value.node.get() == element) { return {static_cast<std::uint32_t>(i), slot.generation}; }
    }
    return insertSlot<UiElement>(m_impl->elements, Impl::Element{element->GetObserverPtr(), {}});
}

bool GameUi::setText(UiElement handle, std::string_view text) {
    auto* element = m_impl->element(handle);
    if (!element) { return false; }
    if (!element->text || *element->text != text) {
        UiFileInterface::Scope files(m_impl->reader);
        std::string value(text);
        element->node->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
        element->text = std::move(value);
    }
    return true;
}

bool GameUi::setRml(UiElement handle, std::string_view markup) {
    auto* element = m_impl->element(handle);
    if (!element) { return false; }
    UiFileInterface::Scope files(m_impl->reader);
    element->node->SetInnerRML(std::string(markup));
    element->text.reset();
    return true;
}

bool GameUi::setClass(UiElement handle, std::string_view name, bool enabled) {
    auto* element = m_impl->element(handle);
    if (!element) { return false; }
    element->node->SetClass(std::string(name), enabled);
    return true;
}

bool GameUi::setProperty(UiElement handle, std::string_view name, std::string_view value) {
    auto* element = m_impl->element(handle);
    return element && element->node->SetProperty(std::string(name), std::string(value));
}

bool GameUi::removeProperty(UiElement handle, std::string_view name) {
    auto* element = m_impl->element(handle);
    if (!element) { return false; }
    element->node->RemoveProperty(std::string(name));
    return true;
}

bool GameUi::setAttribute(UiElement handle, std::string_view name, std::string_view value) {
    auto* element = m_impl->element(handle);
    if (!element) { return false; }
    UiFileInterface::Scope files(m_impl->reader);
    element->node->SetAttribute(std::string(name), std::string(value));
    return true;
}

std::optional<std::string> GameUi::value(UiElement handle) const {
    const auto* element = m_impl->element(handle);
    if (!element) { return std::nullopt; }
    return uiElementValue(*element->node.get());
}

UiListener GameUi::subscribe(UiElement handle, std::string_view eventName, UiOwner owner) {
    auto* element = m_impl->element(handle);
    return element ? m_impl->events.subscribe(*element->node.get(), std::string(eventName), owner) : 0;
}

bool GameUi::subscribed(UiListener listener, UiOwner owner) const {
    return m_impl->events.subscribed(listener, owner);
}

std::uint64_t GameUi::listenerRevision() const { return m_impl->events.revision(); }

bool GameUi::unsubscribe(UiListener listener, UiOwner owner) {
    return m_impl->events.unsubscribe(listener, owner);
}

void GameUi::unsubscribeOwner(UiOwner owner) { m_impl->events.unsubscribeOwner(owner); }

std::span<const UiEvent> GameUi::takeEvents() { return m_impl->events.takeEvents(); }

bool GameUi::wantsMouse() const {
    if (!valid()) { return false; }
    const auto* hover = m_impl->context->GetHoverElement();
    return m_impl->mouseInteracting || (m_impl->pointerInside && hover && hover->GetOwnerDocument());
}

bool GameUi::wantsKeyboard() const {
    if (!valid() || !m_impl->keyboardActive) { return false; }
    const auto* focus = m_impl->context->GetFocusElement();
    return focus && focus->GetOwnerDocument() && focus != focus->GetOwnerDocument();
}

void GameUi::clear() {
    if (!valid()) { return; }
    auto& impl = *m_impl;
    impl.events.clear();
    for (auto& slot : impl.documents) { slot.value = {}; }
    for (auto& slot : impl.elements) { slot.value = {}; }
    impl.context->UnloadAllDocuments();
    impl.context->Update();
    for (const auto& [name, model] : impl.models) {
        impl.context->RemoveDataModel(name);
    }
    impl.models.clear();
    impl.sceneDocuments.clear();
    impl.sceneRevision.reset();
    impl.scene = nullptr;
    impl.context->ProcessMouseLeave();
    impl.mouseDown = {};
    impl.keyDown = {};
    impl.pointerInside = impl.keyboardActive = impl.mouseInteracting = false;
    clearMarkupCaches();
}

} // namespace sat
