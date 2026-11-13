#pragma once

#include "ShaderLibrary.hpp"

#include <slang.h>

#include <array>
#include <chrono>
#include <filesystem>

namespace sat {

class ShaderHotReload {
public:
    [[nodiscard]] bool compileProjectPixel(std::string_view path, std::string_view source, std::string& bytecode);
    void poll(ID3D11Device* device, ShaderLibrary& library);
private:
    Microsoft::WRL::ComPtr<slang::IGlobalSession> m_session;
    std::array<std::filesystem::file_time_type, kShaderNames.size() + 5> m_times{};
    std::chrono::steady_clock::time_point m_nextPoll{};
    bool m_initialized = false;
};

} // namespace sat
