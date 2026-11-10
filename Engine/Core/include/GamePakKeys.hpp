#pragma once

#include "PakArchive.hpp"

#include <string_view>

namespace sat {
// Implemented by a private, externally generated translation unit. No fallback.
// The caller must clear the temporary key after loading the corresponding group.
[[nodiscard]] bool loadGamePakKey(std::string_view group, PakKey& result);
} // namespace sat
