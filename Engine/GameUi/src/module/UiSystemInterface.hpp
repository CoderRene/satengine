#pragma once

#include <RmlUi/Core/SystemInterface.h>

#include <cstdint>
#include <chrono>

namespace sat {

class UiSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override;
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override;

    [[nodiscard]] std::uint64_t errors() const { return m_errors; }

private:
    std::uint64_t m_errors = 0;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

} // namespace sat
