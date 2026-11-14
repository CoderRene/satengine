#pragma once

#include "AssetLibrary.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <string_view>

namespace sat {

class Scene;
class Input;

struct UiDocument {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};

struct UiElement {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};

using UiListener = std::uint64_t;
using UiOwner = std::uintptr_t;

using UiDataValue = std::variant<bool, double, std::string>;
using UiDataField = std::pair<std::string, UiDataValue>;

struct UiEvent {
    UiListener listener;
    UiOwner owner;
    std::string type;
    int x;
    int y;
    int key;
    std::string value;
};

// Window-thread only. The borrowed render interface and reader captures must
// outlive this object. Closing a document invalidates all its element handles.
class GameUi {
public:
    GameUi(void* rmlRenderInterface, AssetReader reader);
    ~GameUi();
    GameUi(const GameUi&) = delete;
    GameUi& operator=(const GameUi&) = delete;
    GameUi(GameUi&&) = delete;
    GameUi& operator=(GameUi&&) = delete;

    [[nodiscard]] bool valid() const;
    void setViewport(int width, int height);
    // Window-client coordinates map into the viewport using its origin and scale.
    void processInput(const Input& input, float offsetX, float offsetY,
        float scaleX = 1.0f, float scaleY = 1.0f, bool pointer = true, bool keyboard = true);
    void update();
    [[nodiscard]] bool hasDocuments() const;
    void render();

    // Enable polling for editable assets. Packed projects leave this disabled.
    void enableHotReload();
    bool createModel(std::string_view name, std::span<const UiDataField> fields, UiOwner owner);
    bool setModelValue(std::string_view model, std::string_view field, UiDataValue value);
    [[nodiscard]] std::optional<UiDataValue> modelValue(std::string_view model, std::string_view field) const;
    void removeModels(UiOwner owner);
    // Synchronize authored components and retain handles until their path changes.
    [[nodiscard]] std::string syncScene(const Scene& scene);
    [[nodiscard]] UiDocument sceneDocument(const Scene& scene, std::uint64_t entity);
    void clearScene();

    [[nodiscard]] bool loadFont(std::string_view path);
    [[nodiscard]] UiDocument loadDocument(std::string_view path);
    bool show(UiDocument document);
    bool hide(UiDocument document);
    bool close(UiDocument document);
    [[nodiscard]] UiElement element(UiDocument document, std::string_view id);
    bool setText(UiElement element, std::string_view text);
    bool setRml(UiElement element, std::string_view markup);
    bool setClass(UiElement element, std::string_view name, bool enabled);
    bool setProperty(UiElement element, std::string_view name, std::string_view value);
    bool removeProperty(UiElement element, std::string_view name);
    bool setAttribute(UiElement element, std::string_view name, std::string_view value);
    [[nodiscard]] std::optional<std::string> value(UiElement element) const;

    [[nodiscard]] UiListener subscribe(UiElement element, std::string_view eventName, UiOwner owner);
    [[nodiscard]] bool subscribed(UiListener listener, UiOwner owner) const;
    [[nodiscard]] std::uint64_t listenerRevision() const;
    bool unsubscribe(UiListener listener, UiOwner owner);
    void unsubscribeOwner(UiOwner owner);
    // Borrowed delivery batch, valid until the next takeEvents() or destruction.
    [[nodiscard]] std::span<const UiEvent> takeEvents();

    [[nodiscard]] bool wantsMouse() const;
    [[nodiscard]] bool wantsKeyboard() const;
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sat
