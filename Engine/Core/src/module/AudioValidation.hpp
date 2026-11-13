#pragma once

#include "AudioTypes.hpp"

#include <cmath>

namespace sat {

inline bool validAudioGain(float value) {
    return std::isfinite(value) && value >= 0.0f;
}

inline bool validAudioPitch(float value) {
    return std::isfinite(value) && value > 0.0f;
}

inline bool validAudioBus(AudioBus bus) {
    return static_cast<std::size_t>(bus) < kAudioBusCount;
}

inline bool validAudioSettings(const AudioSettings& settings) {
    if (!validAudioGain(settings.masterVolume) || !validAudioPitch(settings.speedOfSound) ||
        settings.maxVoices == 0 || settings.maxVoices > kMaxAudioVoices ||
        settings.maxVirtualVoices > kMaxAudioVoices) {
        return false;
    }
    for (float volume : settings.busVolumes) {
        if (!validAudioGain(volume)) {
            return false;
        }
    }
    return true;
}

inline bool validAudioSource(const AudioSourceComponent& source) {
    return validAudioGain(source.volume) && validAudioPitch(source.pitch) &&
        validAudioPitch(source.minDistance) && std::isfinite(source.maxDistance) &&
        source.maxDistance >= source.minDistance && validAudioGain(source.rolloff) &&
        validAudioGain(source.dopplerFactor) && validAudioBus(source.bus);
}

} // namespace sat
