# Resource discovery uses CONFIGURE_DEPENDS so adding/removing Lua files also
# updates the bundle. C++ target sources remain explicitly registered.
function(sat_embed_lua_project target project_dir)
    get_filename_component(project_dir "${project_dir}" ABSOLUTE)
    if(NOT EXISTS "${project_dir}/SatEngineProject.sat" OR
       NOT EXISTS "${project_dir}/Scene.sat")
        message(FATAL_ERROR "Lua game project must contain SatEngineProject.sat and Scene.sat (save the scene in the editor first): ${project_dir}")
    endif()
    file(GLOB_RECURSE scripts CONFIGURE_DEPENDS "${project_dir}/Scripts/*.lua")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_dir}/EmbeddedLuaProject.hpp"
        "#pragma once\n#include \"LuaRuntime.hpp\"\nnamespace sat {\n[[nodiscard]] std::span<const LuaScriptSource> embeddedLuaScripts();\n[[nodiscard]] std::string_view embeddedLuaScene();\n}\n")
    # The manifest changes for project switches and file removal as well as additions.
    string(JOIN "\n" manifest ${scripts})
    file(GENERATE OUTPUT "${generated_dir}/Scripts.manifest"
        CONTENT "${project_dir}\n${manifest}\n")
    add_custom_command(
        OUTPUT "${generated_dir}/EmbeddedLuaProject.cpp"
        COMMAND "${CMAKE_COMMAND}"
            "-DSAT_SCRIPT_ROOT=${project_dir}/Scripts"
            "-DSAT_SCENE_FILE=${project_dir}/Scene.sat"
            "-DSAT_OUTPUT=${generated_dir}/EmbeddedLuaProject.cpp"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateLuaBundle.cmake"
        DEPENDS "${project_dir}/Scene.sat" "${generated_dir}/Scripts.manifest" ${scripts} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateLuaBundle.cmake"
        COMMENT "Embedding Lua project into ${target}"
        VERBATIM
    )
    add_library(${target} STATIC "${generated_dir}/EmbeddedLuaProject.cpp")
    target_include_directories(${target} PUBLIC "${generated_dir}")
    target_link_libraries(${target} PUBLIC SatScripting)
endfunction()
