#include "ShaderLibrary.hpp"

#include "ShaderLayout.hpp"
#include "ShaderManifest.hpp"

#include <iostream>
#include <iterator>
#include <span>
#include <utility>

namespace sat {

void ShaderLibrary::setReader(AssetReader reader) {
    release();
    m_reader = std::move(reader);
}

void ShaderLibrary::setProjectReader(AssetReader reader) {
    m_projectShaders.clear();
    m_projectReader = std::move(reader);
    ++m_revision;
}

void ShaderLibrary::setProjectCompiler(ProjectCompiler compiler) {
    m_projectCompiler = std::move(compiler);
}

void ShaderLibrary::loadProjectPixel(ID3D11Device* device, std::string_view path, ProjectShader& entry) {
    constexpr std::size_t kMaxShaderBytes = 1024 * 1024;
    std::string bytes, error;
    if (m_projectCompiler) {
        std::string source;
        const bool found = m_projectReader && m_projectReader(path, source, error) == AssetReadResult::Found &&
            !source.empty() && source.size() <= kMaxShaderBytes;
        if (!found) { source.clear(); }
        if (entry.attempted && source == entry.source) { return; }
        entry.attempted = true;
        entry.source = std::move(source);
        if (!found) {
            std::cerr << path << ": " << (error.empty() ? "Missing, empty, or oversized project shader" : error) << '\n';
            return;
        }
        if (!m_projectCompiler(path, entry.source, bytes)) { return; }
    } else {
        const std::string compiled = "Shaders/Project/" + std::string(path) + ".dxbc";
        if (!m_reader || m_reader(compiled, bytes, error) != AssetReadResult::Found) {
            std::cerr << path << ": " << (error.empty() ? "Compiled project shader unavailable" : error) << '\n';
            return;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11PixelShader> candidate;
    if (bytes.empty() || bytes.size() > kMaxShaderBytes ||
        FAILED(device->CreatePixelShader(bytes.data(), bytes.size(), nullptr, candidate.GetAddressOf()))) {
        std::cerr << path << ": cannot create project pixel shader\n";
        return;
    }
    entry.pixel = std::move(candidate);
    ++m_revision;
}

ID3D11PixelShader* ShaderLibrary::projectPixel(ID3D11Device* device, std::string_view path) {
    if (const auto found = m_projectShaders.find(path); found != m_projectShaders.end()) {
        return found->second.pixel.Get();
    }
    if (!isShaderAssetPath(path) || m_projectShaders.size() >= 256) { return nullptr; }
    auto& entry = m_projectShaders.try_emplace(std::string(path)).first->second;
    loadProjectPixel(device, path, entry);
    return entry.pixel.Get();
}

void ShaderLibrary::pollProjectShaders(ID3D11Device* device, bool sharedChanged) {
    if (!m_projectCompiler) { return; }
    for (auto& [path, shader] : m_projectShaders) {
        if (sharedChanged) { shader.attempted = false; }
        loadProjectPixel(device, path, shader);
    }
}

void ShaderLibrary::release() {
    ++m_revision;
    m_programs = {};
    m_projectShaders.clear();
    m_ready = m_failed = false;
}

ShaderProgram ShaderLibrary::program(ShaderId id) const {
    const auto& value = m_programs[static_cast<std::size_t>(id)];
    return {value.vertex.Get(), value.pixel.Get(), value.layout.Get()};
}

bool ShaderLibrary::replace(ID3D11Device* device, ShaderId id, std::string_view vs, std::string_view ps) {
    constexpr std::span<const D3D11_INPUT_ELEMENT_DESC> kLayouts[]{
        kMeshLayout, kSelectionOutlineLayout, kCameraFrustumLayout, kTransformGizmoLayout, kGridLayout, {}, {}
    };
    static_assert(std::size(kLayouts) == kShaderNames.size());
    const auto index = static_cast<std::size_t>(id);
    const auto layout = kLayouts[index];
    Program candidate;
    if (FAILED(device->CreateVertexShader(vs.data(), vs.size(), nullptr, candidate.vertex.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(ps.data(), ps.size(), nullptr, candidate.pixel.GetAddressOf())) ||
        (!layout.empty() && FAILED(device->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()),
            vs.data(), vs.size(), candidate.layout.GetAddressOf())))) {
        return false;
    }
    m_programs[index] = std::move(candidate);
    ++m_revision;
    return true;
}

bool ShaderLibrary::ensure(ID3D11Device* device) {
    if (m_ready || m_failed || !device) { return m_ready; }
    std::string error;
    const auto fail = [&](std::string_view message) {
        release();
        m_failed = true;
        std::cerr << message << '\n';
        return false;
    };
    const auto read = [&](const std::string& path, std::string& bytes) {
        error.clear();
        if (m_reader && m_reader(path, bytes, error) == AssetReadResult::Found &&
            !bytes.empty() && bytes.size() <= 1024 * 1024) { return true; }
        error = path + ": " + (error.empty() ? "missing, empty, or oversized shader file" : error);
        return false;
    };
    std::string manifest;
    if (!read("Shaders/ShaderManifest", manifest)) { return fail(error); }
    std::string expected = "SatShaders 1\n";
    expected += kShaderLayoutHash;
    expected += '\n';
    for (const auto* name : kShaderNames) {
        expected += std::string(name) + ".vs.dxbc\n" + name + ".ps.dxbc\n";
    }
    // CMake can write CRLF on Windows. Keep the manifest comparison strict
    // after normalizing line endings.
    std::size_t length = 0;
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        if (manifest[i] == '\r' && i + 1 < manifest.size() && manifest[i + 1] == '\n') { continue; }
        manifest[length++] = manifest[i];
    }
    manifest.resize(length);
    if (manifest != expected) { return fail("Shader archive does not match this build"); }
    for (std::size_t i = 0; i < kShaderNames.size(); ++i) {
        const auto prefix = std::string("Shaders/") + kShaderNames[i];
        std::string vs, ps;
        if (!read(prefix + ".vs.dxbc", vs) || !read(prefix + ".ps.dxbc", ps)) { return fail(error); }
        if (!replace(device, static_cast<ShaderId>(i), vs, ps)) { return fail("Cannot create shader program " + prefix); }
    }
    m_ready = true;
    return true;
}

} // namespace sat
