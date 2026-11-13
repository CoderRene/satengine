# Keep the pinned backend intact on disk. Replace its runtime shader compiler
# in the generated translation unit while retaining its device/state handling.
set(imgui_dx11 "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${imgui_dx11}")
file(READ "${imgui_dx11}" backend)
foreach(stage vertex pixel)
    if(NOT backend MATCHES "static const char\\* ${stage}Shader =")
        message(FATAL_ERROR "The ImGui DX11 shader initialization changed. Update ImGuiShaders.cmake.")
    endif()
    string(FIND "${backend}" "static const char* ${stage}Shader =" start)
    string(SUBSTRING "${backend}" ${start} -1 tail)
    string(FIND "${tail}" "if (bd->pd3dDevice->Create" stop)
    if(stop LESS 0)
        message(FATAL_ERROR "Cannot locate ImGui DX11 shader creation")
    endif()
    math(EXPR end "${start} + ${stop}")
    string(SUBSTRING "${backend}" 0 ${start} head)
    string(SUBSTRING "${backend}" ${end} -1 tail)
    if(stage STREQUAL "vertex")
        set(short vs)
    else()
        set(short ps)
    endif()
    set(backend "${head}std::string ${stage}ShaderBlob;\n        if (!loadSatImGuiShader(\"Shaders/ImGui.${short}.dxbc\", ${stage}ShaderBlob))\n            return false;\n        ${tail}")
    string(REPLACE "${stage}ShaderBlob->GetBufferPointer()" "${stage}ShaderBlob.data()" backend "${backend}")
    string(REPLACE "${stage}ShaderBlob->GetBufferSize()" "${stage}ShaderBlob.size()" backend "${backend}")
    string(REGEX REPLACE "[ \t]*${stage}ShaderBlob->Release\\(\\);[\r\n]+" "" backend "${backend}")
endforeach()
string(REPLACE "#include <d3dcompiler.h>" [=[
#include "AssetLibrary.hpp"
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <Windows.h>

static bool loadSatImGuiShader(const char* name, std::string& bytes)
{
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    std::string error;
    if (length > 0 && length < std::size(executable) &&
        sat::fileShaderReader(std::filesystem::path(executable).parent_path() / "EditorShaders")(name, bytes, error) ==
            sat::AssetReadResult::Found && !bytes.empty())
    {
        return true;
    }
    std::cerr << "Cannot load " << name << ": " << error << '\n';
    return false;
}
]=] backend "${backend}")
string(REGEX REPLACE "#pragma comment\\(lib, \"d3dcompiler\"\\)[^\n]*" "" backend "${backend}")
if(backend MATCHES "D3DCompile\\(")
    string(REGEX REPLACE "    // By using D3DCompile[^
]*(
    //[^
]*)*" "" backend "${backend}")
endif()
if(backend MATCHES "if \\(FAILED\\(D3DCompile|#include <d3dcompiler.h>")
    message(FATAL_ERROR "The generated ImGui DX11 backend still requires D3DCompiler")
endif()
set(imgui_dx11_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_impl_dx11.cpp")
file(GENERATE OUTPUT "${imgui_dx11_generated}" CONTENT "${backend}")

foreach(stage vs ps)
    if(stage STREQUAL "vs")
        set(shader_stage vertex)
    else()
        set(shader_stage fragment)
    endif()
    set(output "${CMAKE_BINARY_DIR}/EditorShaders/ImGui.${stage}.dxbc")
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/EditorShaders"
        COMMAND "${sat_slangc}" "${shader_source}/ImGui.slang" -entry "${stage}Main" -stage "${shader_stage}"
            ${shader_flags} -o "${output}"
        DEPENDS "${shader_source}/ImGui.slang" "${sat_slangc}"
        VERBATIM)
    list(APPEND imgui_shader_outputs "${output}")
endforeach()
add_custom_target(SatEditorShaders DEPENDS ${imgui_shader_outputs})
set(SAT_EDITOR_SHADER_OUTPUTS "${imgui_shader_outputs}" CACHE INTERNAL "Compiled ImGui shader files")
