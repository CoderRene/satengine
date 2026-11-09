include(FetchContent)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)

# imgui has no CMakeLists.txt; Populate only downloads sources.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(SatEngine STATIC
    Core/src/Application.cpp
    Core/src/Window.cpp
    Core/src/Project.cpp
    Core/src/RecentProjects.cpp
    Core/src/Scene.cpp
    Platform/src/FileDialog.cpp
    Renderer/src/Renderer.cpp
    Renderer/src/module/MeshPass.cpp
    Renderer/src/module/SelectionOutlinePass.cpp
    Renderer/src/module/CameraFrustumPass.cpp
    Renderer/src/module/TranslateGizmoPass.cpp
    UI/src/UiLayer.cpp
    UI/src/HierarchyPanel.cpp
    UI/src/InspectorPanel.cpp
    UI/src/ContentPanel.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(SatEngine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/UI/include"
    "${glfw_SOURCE_DIR}/include"
)

target_include_directories(SatEngine PRIVATE
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/src"
)

target_compile_features(SatEngine PUBLIC cxx_std_20)

# Absolute assets root so font/asset loads work regardless of process CWD.
# Written into Core/include so IDE/IntelliSense can resolve EnginePaths.hpp.
set(SAT_ENGINE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
file(TO_CMAKE_PATH "${SAT_ENGINE_ASSETS_DIR}" SAT_ENGINE_ASSETS_DIR)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/include/EnginePaths.hpp.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/include/EnginePaths.hpp"
    @ONLY
)

target_link_libraries(SatEngine PUBLIC glfw)

if(WIN32)
    target_link_libraries(SatEngine PRIVATE d3d11 dxgi d3dcompiler dwmapi ole32 uuid)
endif()
