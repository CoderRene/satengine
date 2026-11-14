#pragma once

#include "Scene.hpp"

#include <imgui.h>

#include <cstring>

namespace sat {

inline constexpr char kEntityPayload[] = "SAT_HIERARCHY_ENTITY";

inline EntityId hierarchyPayloadEntity(const ImGuiPayload* payload) {
    EntityId id = kInvalidEntityId;
    if (payload && payload->IsDataType(kEntityPayload) && payload->DataSize == sizeof(id)) {
        std::memcpy(&id, payload->Data, sizeof(id));
    }
    return id;
}

} // namespace sat
