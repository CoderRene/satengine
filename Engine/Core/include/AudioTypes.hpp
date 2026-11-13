#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sat {

enum class AudioBus { Sfx, Music, Ui };
inline constexpr std::size_t kAudioBusCount = 3;
inline constexpr std::uint32_t kMaxAudioVoices = 1024;

// Left-handed world space, +Y up and +Z forward. Distances are in meters,
// volumes are linear gains, and pitch, rolloff and doppler are ratios.
struct AudioSourceComponent {
    std::string clipPath;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool playOnStart = true;
    bool spatial = true;
    float minDistance = 1.0f;
    float maxDistance = 100.0f;
    float rolloff = 1.0f;
    float dopplerFactor = 1.0f;
    AudioBus bus = AudioBus::Sfx;
    int priority = 0;

    bool operator==(const AudioSourceComponent&) const = default;
};

struct AudioListenerComponent {};

struct AudioSettings {
    float masterVolume = 1.0f;
    float busVolumes[kAudioBusCount] = {1.0f, 1.0f, 1.0f};
    float speedOfSound = 343.0f; // Meters per second.
    std::uint32_t maxVoices = 64;
    std::uint32_t maxVirtualVoices = 0;
    bool followTimeScale[kAudioBusCount] = {};
};

} // namespace sat
