FetchContent_Declare(slang
    URL "https://github.com/shader-slang/slang/releases/download/v2026.17.1/slang-2026.17.1-windows-x86_64.zip"
    URL_HASH SHA256=a41896cba523ecfecaf67d18bdb7767349112a45540ac0b88e8ce48e10ce239f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(slang)
find_program(sat_slangc NAMES slangc PATHS "${slang_SOURCE_DIR}/bin" "${slang_SOURCE_DIR}" NO_DEFAULT_PATH REQUIRED)
set(SAT_SLANGC "${sat_slangc}" CACHE INTERNAL "Slang command-line compiler")
set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/shaders")
set(shader_dir "${CMAKE_BINARY_DIR}/Shaders")
set(shader_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(shader_layout "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/src/module/ShaderLayout.hpp")
set(shader_shared Common Brdf Lighting Material MeshFragment)
list(TRANSFORM shader_shared PREPEND "${shader_source}/")
list(TRANSFORM shader_shared APPEND ".slang")
set(shader_flags -target dxbc -profile sm_5_0 -matrix-layout-column-major -O3)
set(SAT_SHADER_FLAGS "${shader_flags}" CACHE INTERNAL "Slang compile flags")
foreach(pass Mesh SelectionOutline CameraFrustum TransformGizmo Grid PostProcess Shadow)
    foreach(stage vs ps)
        if(stage STREQUAL "vs")
            set(shader_stage vertex)
        else()
            set(shader_stage fragment)
        endif()
        set(output "${shader_dir}/${pass}.${stage}.dxbc")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_dir}"
            COMMAND "${sat_slangc}" "${shader_source}/${pass}.slang" -entry "${stage}Main" -stage "${shader_stage}"
                ${shader_flags} -I "${shader_source}" -o "${output}"
            DEPENDS "${shader_source}/${pass}.slang" ${shader_shared} "${sat_slangc}"
            VERBATIM)
        list(APPEND shader_outputs "${output}")
    endforeach()
endforeach()
string(REPLACE ";" "|" shader_entries "${shader_outputs}")
add_custom_command(OUTPUT "${shader_dir}/ShaderManifest" "${shader_generated}/ShaderManifest.hpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${shader_dir}" "${shader_generated}"
    COMMAND ${CMAKE_COMMAND} "-DLAYOUT=${shader_layout}" "-DSHADER_DIR=${shader_dir}"
        "-DGENERATED=${shader_generated}" "-DENTRIES=${shader_entries}"
        -P "${PROJECT_SOURCE_DIR}/scripts/ShaderManifest.cmake"
    DEPENDS "${shader_layout}" "${CMAKE_CURRENT_SOURCE_DIR}/Core/include/AssetData.hpp"
        "${PROJECT_SOURCE_DIR}/scripts/ShaderManifest.cmake"
    VERBATIM)
list(APPEND shader_outputs "${shader_dir}/ShaderManifest")
add_custom_target(SatShaders DEPENDS ${shader_outputs} "${shader_generated}/ShaderManifest.hpp")
set(SAT_SHADER_OUTPUTS "${shader_outputs}" CACHE INTERNAL "Compiled shader files")

option(SAT_SHADER_HOT_RELOAD "Recompile changed shaders in development configurations" ${SAT_BUILD_EDITOR})
if(SAT_SHADER_HOT_RELOAD)
    find_library(sat_slang_lib NAMES slang-compiler slang PATHS "${slang_SOURCE_DIR}/lib" NO_DEFAULT_PATH REQUIRED)
    find_file(sat_slang_dll NAMES slang-compiler.dll slang.dll PATHS "${slang_SOURCE_DIR}/bin" NO_DEFAULT_PATH REQUIRED)
    set(SAT_SLANG_DLL "${sat_slang_dll}" CACHE INTERNAL "Development Slang compiler")
    string(JOIN "\", \"" shader_flags_cpp ${shader_flags})
    file(GENERATE OUTPUT "${shader_generated}/ShaderCompilerOptions.hpp"
        CONTENT "#pragma once\nnamespace sat { inline constexpr const char* kShaderCompilerOptions[] = {\"${shader_flags_cpp}\", \"-I\", \"${shader_source}\"}; }\n")
endif()
