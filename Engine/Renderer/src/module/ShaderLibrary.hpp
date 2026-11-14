#pragma once

#include "AssetLibrary.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <unordered_map>

namespace sat {

enum class ShaderId : std::uint8_t { Mesh, SelectionOutline, CameraFrustum, TransformGizmo, Grid, PostProcess, Shadow, Ui, UiEffects, UiGradient, Count };
inline constexpr std::array kShaderNames{"Mesh", "SelectionOutline", "CameraFrustum", "TransformGizmo", "Grid", "PostProcess", "Shadow", "Ui", "UiEffects", "UiGradient"};

struct ShaderProgram {
    ID3D11VertexShader* vertex;
    ID3D11PixelShader* pixel;
    ID3D11InputLayout* layout;
};

// Device-thread only. Borrowed programs expire on replacement or release.
class ShaderLibrary {
public:
    using ProjectCompiler = std::function<bool(std::string_view, std::string_view, std::string&)>;
    void setReader(AssetReader reader);
    void setProjectReader(AssetReader reader);
    void setProjectCompiler(ProjectCompiler compiler);
    void pollProjectShaders(ID3D11Device* device, bool sharedChanged);
    [[nodiscard]] ID3D11PixelShader* projectPixel(ID3D11Device* device, std::string_view path);
    void release();
    [[nodiscard]] std::uint64_t revision() const { return m_revision; }
    [[nodiscard]] bool ensure(ID3D11Device* device);
    [[nodiscard]] ShaderProgram program(ShaderId id) const;
    [[nodiscard]] bool replace(ID3D11Device* device, ShaderId id, std::string_view vs, std::string_view ps);
private:
    struct Program {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> layout;
    };
    std::array<Program, static_cast<std::size_t>(ShaderId::Count)> m_programs;
    struct ShaderPathHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view path) const { return std::hash<std::string_view>{}(path); }
    };
    struct ProjectShader {
        Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
        std::string source;
        bool attempted = false;
    };
    void loadProjectPixel(ID3D11Device* device, std::string_view path, ProjectShader& entry);
    std::unordered_map<std::string, ProjectShader, ShaderPathHash, std::equal_to<>> m_projectShaders;
    AssetReader m_projectReader;
    ProjectCompiler m_projectCompiler;
    AssetReader m_reader;
    std::uint64_t m_revision = 0;
    bool m_ready = false;
    bool m_failed = false;
};

} // namespace sat
