#pragma once

#include "GameUi.hpp"

#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Types.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace sat {

class UiEventQueue {
public:
    UiEventQueue();
    ~UiEventQueue();
    [[nodiscard]] UiListener subscribe(Rml::Element& element, const Rml::String& type, UiOwner owner);
    [[nodiscard]] bool subscribed(UiListener listener, UiOwner owner) const;
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }
    bool unsubscribe(UiListener listener, UiOwner owner);
    void unsubscribeOwner(UiOwner owner);
    void unsubscribeDocument(const Rml::ElementDocument& document);
    void rebindDocument(const Rml::ElementDocument& previous, Rml::ElementDocument& replacement);
    void pruneDetached();
    [[nodiscard]] std::span<const UiEvent> takeEvents();
    void clear();

private:
    class Listener;
    std::unordered_map<UiListener, std::unique_ptr<Listener>> m_listeners;
    std::vector<UiEvent> m_pending;
    std::vector<UiEvent> m_delivery;
    std::uint64_t m_revision = 0;
    UiListener m_next = 1;
};

[[nodiscard]] Rml::String uiElementValue(const Rml::Element& element);

} // namespace sat
