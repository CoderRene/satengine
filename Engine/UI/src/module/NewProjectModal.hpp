#pragma once

#include <cstddef>
#include <filesystem>

namespace sat {

void drawNewProjectModal(bool& open, const std::filesystem::path& parent, char* name, std::size_t nameSize,
    bool (*create)(void* context), void* context);

} // namespace sat
