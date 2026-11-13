#pragma once

#include "Math.hpp"
#include "Scene.hpp"

#include <miniaudio.h>

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

namespace sat {

struct Voice {
    ma_sound sound{};
    EntityId owner = kInvalidEntityId;
    DVec3 position{};
    DVec3 previousPosition{};
    double dopplerSpeedLimit = 0.0;
    std::uint64_t sequence = 0;
    AudioBus bus = AudioBus::Sfx;
    int priority = 0;
    float pitch = 1.0f;
    float volume = 1.0f;
    EntityId previousListener = kInvalidEntityId;
    bool initialized = false;
    bool spatial = false;
    bool paused = false;
    bool virtualized = false;
    bool mixing = false;
    float appliedPitch = 0.0f;
};

// Slots stay at fixed addresses until clear(). A slot is reserved before
// initializing its sound, and released after the sound is detached from the mixer.
class VoicePool {
public:
    ~VoicePool();
    void reset(std::size_t count);
    void clear();
    [[nodiscard]] bool full() const { return m_free.empty(); }
    [[nodiscard]] Voice* acquire(EntityId owner);
    void release(Voice& voice);
    [[nodiscard]] Voice* find(EntityId owner);
    [[nodiscard]] const Voice* find(EntityId owner) const;
    [[nodiscard]] std::span<Voice> slots() { return m_slots; }

private:
    std::vector<Voice> m_slots;
    std::vector<std::size_t> m_free;
    std::unordered_map<EntityId, std::size_t> m_entities;
};

} // namespace sat
