#include "UiSystemInterface.hpp"

#include <iostream>

namespace sat {

double UiSystemInterface::GetElapsedTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

bool UiSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ||
        (type == Rml::Log::LT_WARNING && message.starts_with("XML parse error"))) {
        ++m_errors;
    }
    std::clog << "Game UI: " << message << '\n';
    return true;
}

} // namespace sat
