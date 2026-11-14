#include "UiEventQueue.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>

#include <utility>

namespace sat {

Rml::String uiElementValue(const Rml::Element& element) {
    if (const auto* control = dynamic_cast<const Rml::ElementFormControl*>(&element)) {
        return control->GetValue();
    }
    return element.GetAttribute<Rml::String>("value", {});
}

class UiEventQueue::Listener final : public Rml::EventListener {
public:
    Listener(std::vector<UiEvent>& pending, UiListener id, UiOwner owner, Rml::String type)
        : m_pending(pending), m_id(id), m_owner(owner), m_type(std::move(type)) {}

    ~Listener() override {
        if (m_element) { m_element->RemoveEventListener(m_type, this); }
    }

    void OnAttach(Rml::Element* element) override { m_element = element; }
    void OnDetach(Rml::Element*) override { m_element = nullptr; }

    void ProcessEvent(Rml::Event& event) override {
        m_pending.push_back({m_id, m_owner, event.GetType(), event.GetParameter("mouse_x", 0),
            event.GetParameter("mouse_y", 0), event.GetParameter("key_identifier", 0),
            event.GetParameter<Rml::String>("value", uiElementValue(*m_element))});
    }

    std::vector<UiEvent>& m_pending;
    UiListener m_id;
    UiOwner m_owner;
    Rml::String m_type;
    Rml::Element* m_element = nullptr;
};

UiEventQueue::UiEventQueue() = default;
UiEventQueue::~UiEventQueue() = default;

UiListener UiEventQueue::subscribe(Rml::Element& element, const Rml::String& type, UiOwner owner) {
    if (type.empty()) { return 0; }
    const auto id = m_next++;
    auto& listener = m_listeners.emplace(id, std::make_unique<Listener>(m_pending, id, owner, type)).first->second;
    element.AddEventListener(type, listener.get());
    return id;
}

bool UiEventQueue::subscribed(UiListener listener, UiOwner owner) const {
    const auto found = m_listeners.find(listener);
    return found != m_listeners.end() && found->second->m_owner == owner && found->second->m_element;
}

bool UiEventQueue::unsubscribe(UiListener listener, UiOwner owner) {
    const auto found = m_listeners.find(listener);
    if (found == m_listeners.end() || found->second->m_owner != owner) { return false; }
    m_listeners.erase(found);
    ++m_revision;
    return true;
}

void UiEventQueue::unsubscribeOwner(UiOwner owner) {
    m_revision += std::erase_if(m_listeners, [owner](const auto& entry) { return entry.second->m_owner == owner; });
}

void UiEventQueue::unsubscribeDocument(const Rml::ElementDocument& document) {
    m_revision += std::erase_if(m_listeners, [&document](const auto& entry) {
        const auto* element = entry.second->m_element;
        return !element || element->GetOwnerDocument() == &document;
    });
}

void UiEventQueue::rebindDocument(const Rml::ElementDocument& previous, Rml::ElementDocument& replacement) {
    m_revision += std::erase_if(m_listeners, [&](auto& entry) {
        auto& listener = *entry.second;
        auto* element = listener.m_element;
        if (!element || element->GetOwnerDocument() != &previous) { return !element; }
        const auto& id = element->GetId();
        auto* next = id.empty() ? nullptr : replacement.GetElementById(id);
        if (!next) { return true; }
        std::erase_if(m_pending, [&](const UiEvent& event) { return event.listener == listener.m_id; });
        element->RemoveEventListener(listener.m_type, &listener);
        next->AddEventListener(listener.m_type, &listener);
        return false;
    });
}

void UiEventQueue::pruneDetached() {
    m_revision += std::erase_if(m_listeners, [](const auto& entry) { return !entry.second->m_element; });
}

std::span<const UiEvent> UiEventQueue::takeEvents() {
    m_delivery.clear();
    m_delivery.swap(m_pending);
    return m_delivery;
}

void UiEventQueue::clear() {
    m_revision += m_listeners.size();
    m_listeners.clear();
    m_pending.clear();
}

} // namespace sat
