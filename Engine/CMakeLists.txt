add_library(SatAssets STATIC
    Core/src/AssetData.cpp
    Core/src/MaterialData.cpp
    Core/src/AssetImportSettings.cpp
    Core/src/AssetImporter.cpp
    Core/src/AssetLibrary.cpp
    Core/src/TextureData.cpp
)
target_include_directories(SatAssets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Core/include")
target_include_directories(SatAssets SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb")
target_compile_features(SatAssets PUBLIC cxx_std_20)

# Scene extraction is portable and can be tested without a window or graphics device.
add_library(SatScene STATIC
    Core/src/Scene.cpp
    Core/src/SceneFile.cpp
    Core/src/module/ScenePhysicsFile.cpp
    Core/src/module/SceneAudioFile.cpp
    Renderer/src/SceneRenderData.cpp
    Renderer/src/SceneDraw.cpp
    Renderer/src/ColliderLines.cpp
    Renderer/src/TransformGizmo.cpp
)
target_include_directories(SatScene PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/include"
)
target_compile_features(SatScene PUBLIC cxx_std_20)
target_link_libraries(SatScene PUBLIC SatAssets)

add_library(SatPak STATIC Core/src/PakArchive.cpp Core/src/module/Pak/PakCrypto.cpp)
if(WIN32)
    target_link_libraries(SatPak PRIVATE bcrypt)
else()
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_link_libraries(SatPak PRIVATE OpenSSL::Crypto)
endif()
target_include_directories(SatPak PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Core/include")
target_compile_features(SatPak PUBLIC cxx_std_20)

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

add_subdirectory(Physics)
add_subdirectory(Audio)

add_library(SatScripting STATIC
    Core/src/LuaRuntime.cpp
    Core/src/Project.cpp
    Core/src/LuaToolingSync.cpp
    Core/src/module/LuaRuntimeModules/LuaSceneBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaInputBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaTimeBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaMessageBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaMathBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaPhysicsBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaAudioBindings.cpp
    Core/src/module/LuaRuntimeModules/LuaPackageConfig.cpp
    Core/src/ScriptLog.cpp
    Core/src/module/LuaRuntimeModules/LuaLogBindings.hpp
    Core/src/module/LuaRuntimeModules/LuaLogBindings.cpp
)
target_include_directories(SatScripting PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/src"
)
target_link_libraries(SatScripting PUBLIC SatScene SatInput SatPhysics SatAudio PRIVATE SatLua)
target_compile_features(SatScripting PUBLIC cxx_std_20)

if(BUILD_TESTING)
    add_executable(PhysicsTests Physics/tests/PhysicsTests.cpp)
    target_link_libraries(PhysicsTests PRIVATE SatPhysics)
    target_compile_definitions(PhysicsTests PRIVATE SAT_PHYSICS_EXAMPLE_DIR="${PROJECT_SOURCE_DIR}/Examples/SpacePhysics")
    add_test(NAME Physics COMMAND PhysicsTests)
    set_tests_properties(Physics PROPERTIES TIMEOUT 60)
    add_executable(PhysicsScriptingTests Physics/tests/PhysicsScriptingTests.cpp)
    target_link_libraries(PhysicsScriptingTests PRIVATE SatScripting)
    add_test(NAME PhysicsScripting COMMAND PhysicsScriptingTests)
    set_tests_properties(PhysicsScripting PROPERTIES TIMEOUT 15)
    add_executable(AudioTests Audio/tests/AudioTests.cpp)
    target_link_libraries(AudioTests PRIVATE SatAudio)
    target_include_directories(AudioTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Audio/src")
    add_test(NAME Audio COMMAND AudioTests)
    add_executable(AudioScriptingTests Audio/tests/AudioScriptingTests.cpp)
    target_link_libraries(AudioScriptingTests PRIVATE SatScripting)
    add_test(NAME AudioScripting COMMAND AudioScriptingTests)
    foreach(audio_test AudioTests AudioScriptingTests)
        target_compile_definitions(${audio_test} PRIVATE SAT_AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Audio/tests/fixtures")
    endforeach()
    set_tests_properties(Audio PROPERTIES TIMEOUT 25)
    set_tests_properties(AudioScripting PROPERTIES TIMEOUT 15)
endif()

# Optional EmmyLua stub regen from // @lua metadata (not required to build/link).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    set(SAT_LUA_DEF_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaSceneBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaInputBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaTimeBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaMessageBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaMathBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaPhysicsBindings.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/src/module/LuaRuntimeModules/LuaAudioBindings.cpp"
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

add_executable(SatPakBuilder "${PROJECT_SOURCE_DIR}/Tools/PakBuilder/main.cpp")
target_link_libraries(SatPakBuilder PRIVATE SatPak SatLua SatScene)
target_include_directories(SatPakBuilder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Core/src")

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

include("${PROJECT_SOURCE_DIR}/cmake/SatShaders.cmake")

# This target must not include/link editor panels, project tools, or ImGui.
add_library(SatRuntime STATIC
    Core/src/Application.cpp
    Core/src/Window.cpp
    Renderer/src/Renderer.cpp
    Renderer/src/module/MeshPass.cpp
    Renderer/src/module/ShaderLibrary.cpp
    Renderer/src/module/GpuAssetCache.cpp
    Renderer/src/module/SelectionOutlinePass.cpp
    Renderer/src/module/CameraFrustumPass.cpp
    Renderer/src/module/TransformGizmoPass.cpp
)
target_include_directories(SatRuntime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/src"
)
target_link_libraries(SatRuntime PUBLIC SatScene SatScripting glfw)
target_link_libraries(SatRuntime PRIVATE d3d11 dxgi dwmapi)
add_dependencies(SatRuntime SatShaders)
target_include_directories(SatRuntime PRIVATE "${shader_generated}")
if(SAT_SHADER_HOT_RELOAD)
    target_sources(SatRuntime PRIVATE "$<$<NOT:$<CONFIG:Release>>:${CMAKE_CURRENT_SOURCE_DIR}/Renderer/src/module/ShaderHotReload.cpp>")
    target_include_directories(SatRuntime PRIVATE "${slang_SOURCE_DIR}/include")
    target_compile_definitions(SatRuntime PRIVATE
        "$<$<NOT:$<CONFIG:Release>>:SAT_SHADER_HOT_RELOAD=1;SAT_SHADER_SOURCE_DIR=\"${shader_source}\">")
    target_link_libraries(SatRuntime PRIVATE "$<$<NOT:$<CONFIG:Release>>:${sat_slang_lib}>")
endif()

if(NOT SAT_BUILD_EDITOR)
    return()
endif()

# Only the editor downloads and compiles ImGui.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG cf53467ab0db7a783f04b32dd9160e6aa5b720ef
)
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

include("${PROJECT_SOURCE_DIR}/cmake/ImGuiShaders.cmake")

add_library(SatEditor STATIC
    Core/src/RecentProjects.cpp
    Platform/src/FileDialog.cpp
    Platform/src/OpenLuaFile.cpp
    Platform/src/EditorLuaDefs.cpp
    Platform/src/GameBuilder.cpp
    UI/src/UiLayer.cpp
    UI/src/module/AssetImportPanel.cpp
    UI/src/module/BuildStatusWindow.cpp
    UI/src/module/CursorWrap.cpp
    UI/src/module/CursorWrap.hpp
    UI/src/module/EditorDiagnostics.cpp
    UI/src/module/EditorDock.cpp
    UI/src/module/EditorFrame.cpp
    UI/src/module/EditorMenuBar.cpp
    UI/src/module/EditorPanels.cpp
    UI/src/module/EditorPrompts.cpp
    UI/src/module/EditorSession.cpp
    UI/src/module/EditorStartup.cpp
    UI/src/module/EditorTheme.cpp
    UI/src/module/NewProjectModal.cpp
    UI/src/module/PhysicsInspector.cpp
    UI/src/module/PlaySession.cpp
    UI/src/module/SceneSettingsWindows.cpp
    UI/src/module/SceneViewGather.cpp
    UI/src/HierarchyPanel.cpp
    UI/src/InspectorPanel.cpp
    UI/src/ContentPanel.cpp
    UI/src/ViewportPanel.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_dx11_generated}
    UI/include/LuaLogPanel.hpp
    UI/src/LuaLogPanel.cpp
)
target_include_directories(SatEditor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_include_directories(SatEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/src"
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
add_dependencies(SatEditor SatEditorShaders)
target_include_directories(SatEditor PRIVATE "${imgui_SOURCE_DIR}/backends")
target_link_libraries(SatEditor PUBLIC SatRuntime)
target_link_libraries(SatEditor PRIVATE d3d11 ole32 uuid shell32)
