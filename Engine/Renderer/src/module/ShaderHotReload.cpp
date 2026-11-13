#include "ShaderHotReload.hpp"

#include "ShaderCompilerOptions.hpp"

#include <iostream>
#include <iterator>

namespace sat {
namespace {
using Microsoft::WRL::ComPtr;

bool compilePass(slang::IGlobalSession& compiler, const char* name, ID3D11Device* device,
    ShaderLibrary& library, ShaderId id) {
    slang::SessionDesc options{};
    ComPtr<ISlangUnknown> optionsStorage;
    ComPtr<slang::ISession> session;
    ComPtr<slang::IBlob> diagnostics;
    const auto fail = [&] {
        std::cerr << "Shader reload failed for " << name;
        if (diagnostics) {
            std::cerr << ": " << std::string_view(static_cast<const char*>(diagnostics->getBufferPointer()),
                diagnostics->getBufferSize());
        }
        std::cerr << '\n';
        return false;
    };
    if (SLANG_FAILED(compiler.parseCommandLineArguments(static_cast<int>(std::size(kShaderCompilerOptions)),
            kShaderCompilerOptions, &options, optionsStorage.GetAddressOf())) ||
        SLANG_FAILED(compiler.createSession(options, session.GetAddressOf()))) { return fail(); }
    auto* module = session->loadModule(name, diagnostics.GetAddressOf());
    if (!module) { return fail(); }

    ComPtr<slang::IBlob> stages[2];
    const char* entries[] = {"vsMain", "psMain"};
    for (std::size_t i = 0; i < std::size(entries); ++i) {
        ComPtr<slang::IEntryPoint> entry;
        if (SLANG_FAILED(module->findEntryPointByName(entries[i], entry.GetAddressOf()))) { return fail(); }
        slang::IComponentType* parts[] = {module, entry.Get()};
        ComPtr<slang::IComponentType> composite, linked;
        if (SLANG_FAILED(session->createCompositeComponentType(parts, 2, composite.GetAddressOf(),
                diagnostics.ReleaseAndGetAddressOf())) ||
            SLANG_FAILED(composite->link(linked.GetAddressOf(), diagnostics.ReleaseAndGetAddressOf())) ||
            SLANG_FAILED(linked->getEntryPointCode(0, 0, stages[i].GetAddressOf(),
                diagnostics.ReleaseAndGetAddressOf()))) { return fail(); }
    }
    const auto bytes = [](const ComPtr<slang::IBlob>& blob) {
        return std::string_view(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
    };
    return library.replace(device, id, bytes(stages[0]), bytes(stages[1])) || fail();
}
}

bool ShaderHotReload::compileProjectPixel(std::string_view path, std::string_view source, std::string& bytecode) {
    slang::SessionDesc options{};
    ComPtr<ISlangUnknown> optionsStorage;
    ComPtr<slang::ISession> session;
    ComPtr<slang::IBlob> diagnostics;
    const auto fail = [&] {
        std::cerr << path << ": project shader compilation failed";
        if (diagnostics) {
            std::cerr << ": " << std::string_view(static_cast<const char*>(diagnostics->getBufferPointer()),
                diagnostics->getBufferSize());
        }
        std::cerr << '\n';
        return false;
    };
    if (!m_session && SLANG_FAILED(slang::createGlobalSession(m_session.GetAddressOf()))) { return fail(); }
    if (SLANG_FAILED(m_session->parseCommandLineArguments(static_cast<int>(std::size(kShaderCompilerOptions)),
            kShaderCompilerOptions, &options, optionsStorage.GetAddressOf())) ||
        SLANG_FAILED(m_session->createSession(options, session.GetAddressOf()))) { return fail(); }
    const std::string name(path);
    const std::string code(source);
    auto* module = session->loadModuleFromSourceString("SatProjectMaterial", name.c_str(), code.c_str(),
        diagnostics.GetAddressOf());
    ComPtr<slang::IEntryPoint> entry;
    if (!module || SLANG_FAILED(module->findEntryPointByName("psMain", entry.GetAddressOf()))) { return fail(); }
    slang::IComponentType* parts[] = {module, entry.Get()};
    ComPtr<slang::IComponentType> composite, linked;
    ComPtr<slang::IBlob> blob;
    if (SLANG_FAILED(session->createCompositeComponentType(parts, 2, composite.GetAddressOf(),
            diagnostics.ReleaseAndGetAddressOf())) ||
        SLANG_FAILED(composite->link(linked.GetAddressOf(), diagnostics.ReleaseAndGetAddressOf())) ||
        SLANG_FAILED(linked->getEntryPointCode(0, 0, blob.GetAddressOf(),
            diagnostics.ReleaseAndGetAddressOf()))) { return fail(); }
    bytecode.assign(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
    return true;
}

void ShaderHotReload::poll(ID3D11Device* device, ShaderLibrary& library) {
    const auto now = std::chrono::steady_clock::now();
    if (now < m_nextPoll) { return; }
    m_nextPoll = now + std::chrono::milliseconds(500);

    std::array<bool, kShaderNames.size()> changed{};
    bool shared = false;
    constexpr const char* kShared[] = {"Common", "Brdf", "Lighting", "Material", "MeshFragment"};
    for (std::size_t i = 0; i < m_times.size(); ++i) {
        const bool pass = i < kShaderNames.size();
        const auto* name = pass ? kShaderNames[i] : kShared[i - kShaderNames.size()];
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(
            std::filesystem::path(SAT_SHADER_SOURCE_DIR) / (std::string(name) + ".slang"), ec);
        if (m_initialized && time != m_times[i]) {
            if (pass) { changed[i] = true; }
            else { shared = true; }
        }
        m_times[i] = time;
    }
    m_initialized = true;
    library.pollProjectShaders(device, shared);
    changed[static_cast<std::size_t>(ShaderId::Mesh)] |= shared;
    changed[static_cast<std::size_t>(ShaderId::Shadow)] |= shared;
    changed[static_cast<std::size_t>(ShaderId::PostProcess)] |= shared;
    for (std::size_t i = 0; i < changed.size(); ++i) {
        if (!changed[i]) { continue; }
        if (!m_session && SLANG_FAILED(slang::createGlobalSession(m_session.GetAddressOf()))) {
            std::cerr << "Cannot initialize Slang shader compiler\n";
            return;
        }
        compilePass(*m_session.Get(), kShaderNames[i], device, library, static_cast<ShaderId>(i));
    }
}

} // namespace sat
