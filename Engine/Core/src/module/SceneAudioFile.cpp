#include "SceneAudioFile.hpp"

#include "AssetData.hpp"
#include "AudioValidation.hpp"
#include "Scene.hpp"

#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>

namespace sat {

void writeAudioSettings(std::ostream& out, const AudioSettings& settings) {
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << settings.masterVolume;
    for (float volume : settings.busVolumes) {
        out << ' ' << volume;
    }
    out << ' ' << settings.speedOfSound << ' ' << settings.maxVoices << ' ' << settings.maxVirtualVoices;
    for (bool follow : settings.followTimeScale) {
        out << ' ' << follow;
    }
    out << '\n';
}

bool readAudioSettings(std::istream& in, AudioSettings& settings, bool extended) {
    if (!(in >> settings.masterVolume)) {
        return false;
    }
    for (float& volume : settings.busVolumes) {
        if (!(in >> volume)) {
            return false;
        }
    }
    if (!(in >> settings.speedOfSound >> settings.maxVoices)) {
        return false;
    }
    if (extended) {
        if (!(in >> settings.maxVirtualVoices)) { return false; }
        for (bool& follow : settings.followTimeScale) {
            if (!(in >> follow)) { return false; }
        }
    }
    return validAudioSettings(settings);
}

void writeAudioComponents(std::ostream& out, const Entity& entity) {
    out << entity.hasAudioSource << ' ' << entity.hasAudioListener << '\n';
    if (entity.hasAudioSource) {
        const auto& source = entity.audioSource;
        out << std::quoted(source.clipPath) << ' ' << source.volume << ' ' << source.pitch << ' '
            << source.loop << ' ' << source.playOnStart << ' ' << source.spatial << ' '
            << source.minDistance << ' ' << source.maxDistance << ' ' << source.rolloff << ' '
            << source.dopplerFactor << ' ' << static_cast<int>(source.bus) << ' ' << source.priority << '\n';
    }
}

bool readAudioComponents(std::istream& in, Entity& entity, std::string& error, bool extended) {
    const auto fail = [&] {
        error = "Invalid audio components on entity " + entity.name;
        return false;
    };
    if (!(in >> entity.hasAudioSource >> entity.hasAudioListener)) {
        return fail();
    }
    if (!entity.hasAudioSource) {
        return true;
    }
    auto& source = entity.audioSource;
    int bus = 0;
    if (!(in >> std::quoted(source.clipPath) >> source.volume >> source.pitch
             >> source.loop >> source.playOnStart >> source.spatial >> source.minDistance
             >> source.maxDistance >> source.rolloff >> source.dopplerFactor >> bus)) {
        return fail();
    }
    if (extended && !(in >> source.priority)) { return fail(); }
    source.bus = static_cast<AudioBus>(bus);
    if (!validAudioSource(source) || (!source.clipPath.empty() && !isAudioAssetPath(source.clipPath))) {
        return fail();
    }
    return true;
}

} // namespace sat
