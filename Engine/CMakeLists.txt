# Scene extraction is portable and can be tested without a window or graphics device.
add_library(SatScene STATIC
    Core/src/Scene.cpp
    Core/src/SceneFile.cpp
    Renderer/src/SceneRenderData.cpp
)
target_include_directories(SatScene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/include"
)
target_compile_features(SatScene PUBLIC cxx_std_20)

include(FetchContent)
# Official Lua release, pinned and checksum verified; no system Lua installation needed.
# Populate only — the tarball has no CMakeLists.txt, so MakeAvailable cannot add_subdirectory.
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
    URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(lua)
if(NOT lua_POPULATED)
    FetchContent_Populate(lua)
endif()
add_library(SatLua STATIC
    ${lua_SOURCE_DIR}/src/lapi.c
    ${lua_SOURCE_DIR}/src/lauxlib.c
    ${lua_SOURCE_DIR}/src/lbaselib.c
    ${lua_SOURCE_DIR}/src/lcode.c
    ${lua_SOURCE_DIR}/src/lcorolib.c
    ${lua_SOURCE_DIR}/src/lctype.c
    ${lua_SOURCE_DIR}/src/ldblib.c
    ${lua_SOURCE_DIR}/src/ldebug.c
    ${lua_SOURCE_DIR}/src/ldo.c
    ${lua_SOURCE_DIR}/src/ldump.c
    ${lua_SOURCE_DIR}/src/lfunc.c
    ${lua_SOURCE_DIR}/src/lgc.c
    ${lua_SOURCE_DIR}/src/linit.c
    ${lua_SOURCE_DIR}/src/liolib.c
    ${lua_SOURCE_DIR}/src/llex.c
    ${lua_SOURCE_DIR}/src/lmathlib.c
    ${lua_SOURCE_DIR}/src/lmem.c
    ${lua_SOURCE_DIR}/src/loadlib.c
    ${lua_SOURCE_DIR}/src/lobject.c
    ${lua_SOURCE_DIR}/src/lopcodes.c
    ${lua_SOURCE_DIR}/src/loslib.c
    ${lua_SOURCE_DIR}/src/lparser.c
    ${lua_SOURCE_DIR}/src/lstate.c
    ${lua_SOURCE_DIR}/src/lstring.c
    ${lua_SOURCE_DIR}/src/lstrlib.c
    ${lua_SOURCE_DIR}/src/ltable.c
    ${lua_SOURCE_DIR}/src/ltablib.c
    ${lua_SOURCE_DIR}/src/ltm.c
    ${lua_SOURCE_DIR}/src/lundump.c
    ${lua_SOURCE_DIR}/src/lutf8lib.c
    ${lua_SOURCE_DIR}/src/lvm.c
    ${lua_SOURCE_DIR}/src/lzio.c
)
# Use Lua's standard-C dispatch instead of compiler-specific computed gotos.
target_compile_definitions(SatLua PRIVATE LUA_USE_JUMPTABLE=0)
target_compile_features(SatLua PRIVATE c_std_99)
target_include_directories(SatLua SYSTEM PUBLIC "${lua_SOURCE_DIR}/src")
if(UNIX)
    target_compile_definitions(SatLua PRIVATE LUA_USE_POSIX)
    target_link_libraries(SatLua PUBLIC m)
endif()

add_library(SatInput STATIC Core/src/Input.cpp
    Core/include/ScriptLog.hpp)
target_include_directories(SatInput PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Core/include")
target_compile_features(SatInput PUBLIC cxx_std_20)

add_library(SatScripting STATIC
    Core/src/LuaRuntime.cpp
    Core/src/Project.cpp
    Core/src/LuaToolingSync.cpp
    Core/src/module/LuaRuntimeModules/LuaSceneBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaInputBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaPackageConfig.cpp
    Core/src/ScriptLog.cpp
    Core/src/module/LuaRuntimeModules/LuaLogBindings.hpp
    Core/src/module/LuaRuntimeModules/LuaLogBindings.cpp
)
target_include_directories(SatScripting PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/src"
)
target_link_libraries(SatScripting PUBLIC SatScene SatInput PRIVATE SatLua)
target_compile_features(SatScripting PUBLIC cxx_std_20)

# Optional EmmyLua stub regen from // @lua metadata (not required to build/link).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(SAT_LUA_DEF_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaSceneBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaInputBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaLogBindings.cpp"
    )
    set(SAT_LUA_DEF_OUTPUT
        "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/LuaDefs/sat.lua"
    )
    add_custom_target(SatLuaDefs
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/generate_lua_defs.py"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/scripts/generate_lua_defs.py"
            ${SAT_LUA_DEF_SOURCES}
        BYPRODUCTS "${SAT_LUA_DEF_OUTPUT}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating EmmyLua stubs (Engine/Scripting/LuaDefs/sat.lua)"
        VERBATIM
    )
else()
    message(STATUS "Python3 not found; skip SatLuaDefs stub generation target")
endif()

if(NOT SAT_BUILD_GAME_PLAYER AND NOT SAT_BUILD_EDITOR)
    return()
endif()

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# This target must not include/link editor panels, project tools, or ImGui.
add_library(SatRuntime STATIC
    Core/src/Application.cpp
    Core/src/Window.cpp
    Renderer/src/Renderer.cpp
    Renderer/src/module/MeshPass.cpp
    Renderer/src/module/SelectionOutlinePass.cpp
    Renderer/src/module/CameraFrustumPass.cpp
    Renderer/src/module/TranslateGizmoPass.cpp
)
target_include_directories(SatRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/src"
)
target_link_libraries(SatRuntime PUBLIC SatScene SatScripting glfw)
target_link_libraries(SatRuntime PRIVATE d3d11 dxgi d3dcompiler dwmapi)

if(NOT SAT_BUILD_EDITOR)
    return()
endif()

# Only the editor downloads and compiles ImGui.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(SatEditor STATIC
    Core/src/RecentProjects.cpp
    Platform/src/FileDialog.cpp
    Platform/src/OpenLuaFile.cpp
    Platform/src/EditorLuaDefs.cpp
    Platform/src/GameBuilder.cpp
    UI/src/UiLayer.cpp
    UI/src/HierarchyPanel.cpp
    UI/src/InspectorPanel.cpp
    UI/src/ContentPanel.cpp
    UI/src/ViewportPanel.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    UI/include/LuaLogPanel.hpp
    UI/src/LuaLogPanel.cpp
)
target_include_directories(SatEditor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(SatEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb"
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
set(SAT_ENGINE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
file(TO_CMAKE_PATH "${SAT_ENGINE_ASSETS_DIR}" SAT_ENGINE_ASSETS_DIR)
set(SAT_ENGINE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
file(TO_CMAKE_PATH "${SAT_ENGINE_SOURCE_DIR}" SAT_ENGINE_SOURCE_DIR)
set(SAT_LUA_DEFS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Scripting/LuaDefs")
file(TO_CMAKE_PATH "${SAT_LUA_DEFS_DIR}" SAT_LUA_DEFS_DIR)
configure_file(Core/include/EnginePaths.hpp.in generated/EnginePaths.hpp @ONLY)
target_link_libraries(SatEditor PUBLIC SatRuntime)
target_link_libraries(SatEditor PRIVATE d3d11 ole32 uuid shell32)
