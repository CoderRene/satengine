#pragma once

#include <iosfwd>
#include <string>

namespace sat {
struct AudioSettings;
struct Entity;

void writeAudioSettings(std::ostream& out, const AudioSettings& settings);
[[nodiscard]] bool readAudioSettings(std::istream& in, AudioSettings& settings, bool extended);
void writeAudioComponents(std::ostream& out, const Entity& entity);
[[nodiscard]] bool readAudioComponents(std::istream& in, Entity& entity, std::string& error, bool extended);

} // namespace sat
