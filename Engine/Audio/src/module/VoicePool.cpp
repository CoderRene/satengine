#include "VoicePool.hpp"

namespace sat {

VoicePool::~VoicePool() {
    clear();
}

void VoicePool::reset(std::size_t count) {
    clear();
    m_slots.resize(count);
    m_free.reserve(count);
    m_entities.reserve(count);
    for (std::size_t index = count; index > 0; --index) {
        m_free.push_back(index - 1);
    }
}

void VoicePool::clear() {
    for (auto& voice : m_slots) {
        if (voice.initialized) {
            ma_sound_uninit(&voice.sound);
        }
    }
    m_entities.clear();
    m_free.clear();
    m_slots.clear();
}

Voice* VoicePool::acquire(EntityId owner) {
    if (m_free.empty()) {
        return nullptr;
    }
    const auto index = m_free.back();
    if (owner != kInvalidEntityId && !m_entities.emplace(owner, index).second) {
        return nullptr;
    }
    m_free.pop_back();
    auto& voice = m_slots[index];
    voice.owner = owner;
    return &voice;
}

void VoicePool::release(Voice& voice) {
    if (voice.initialized) {
        ma_sound_uninit(&voice.sound);
    }
    if (voice.owner != kInvalidEntityId) {
        m_entities.erase(voice.owner);
    }
    voice.initialized = false;
    voice.paused = false;
    voice.virtualized = false;
    voice.mixing = false;
    voice.appliedPitch = 0.0f;
    voice.previousListener = kInvalidEntityId;
    m_free.push_back(static_cast<std::size_t>(&voice - m_slots.data()));
}

Voice* VoicePool::find(EntityId owner) {
    const auto found = m_entities.find(owner);
    return found == m_entities.end() ? nullptr : &m_slots[found->second];
}

const Voice* VoicePool::find(EntityId owner) const {
    const auto found = m_entities.find(owner);
    return found == m_entities.end() ? nullptr : &m_slots[found->second];
}

} // namespace sat
