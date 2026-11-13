add_executable(GamePlayer WIN32
    src/main.cpp
)

set(SAT_GAME_PROJECT "${PROJECT_SOURCE_DIR}/Examples/LuaCube" CACHE PATH
    "Project whose Lua scripts are embedded into the standalone game player")
get_filename_component(_sat_game_project "${SAT_GAME_PROJECT}/." ABSOLUTE)
get_filename_component(_sat_game_name "${_sat_game_project}" NAME)
if(_sat_game_name STREQUAL "")
    message(FATAL_ERROR "The game project must have a folder name for its executable.")
endif()
# Track resource additions/removals as well as content changes.
file(GLOB_RECURSE pak_inputs CONFIGURE_DEPENDS
    "${SAT_GAME_PROJECT}/Scripts/*.lua" "${SAT_GAME_PROJECT}/Assets/*")
set(pak_output "${CMAKE_CURRENT_BINARY_DIR}/${_sat_game_name}.pak")
include("${PROJECT_SOURCE_DIR}/cmake/PrivatePakKeys.cmake")
string(JOIN "\n" pak_manifest ${pak_inputs})
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Pak.manifest"
    CONTENT "${SAT_GAME_PROJECT}\n${pak_manifest}\n")
file(GLOB_RECURSE project_shaders CONFIGURE_DEPENDS "${SAT_GAME_PROJECT}/Assets/*.slang")
list(FILTER project_shaders EXCLUDE REGEX "/\\._[^/]*$")
set(project_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/ProjectShaders")
set(project_shader_outputs)
set(project_shader_args)
foreach(source IN LISTS project_shaders)
    file(RELATIVE_PATH path "${_sat_game_project}" "${source}")
    set(output "${project_shader_dir}/${path}.dxbc")
    get_filename_component(folder "${output}" DIRECTORY)
    add_custom_command(OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${folder}"
        COMMAND "${SAT_SLANGC}" "${source}" -entry psMain -stage fragment
            ${SAT_SHADER_FLAGS} -I "${PROJECT_SOURCE_DIR}/Engine/Renderer/shaders" -o "${output}"
        DEPENDS "${source}" "${SAT_SLANGC}" ${SAT_SHADER_OUTPUTS}
        VERBATIM)
    list(APPEND project_shader_outputs "${output}")
    list(APPEND project_shader_args "${path}")
endforeach()
add_custom_command(OUTPUT "${pak_output}" "${pak_output}.parts"
    COMMAND $<TARGET_FILE:SatPakBuilder> "${SAT_GAME_PROJECT}" "${pak_output}" "${pak_keys}" "${CMAKE_BINARY_DIR}/Shaders" "${project_shader_dir}" ${project_shader_args}
    DEPENDS SatShaders ${SAT_SHADER_OUTPUTS} ${project_shader_outputs} SatPakBuilder "${pak_provider}" "${pak_keys}/scripts.bin" "${pak_keys}/textures.bin"
        "${pak_keys}/audio.bin" "${pak_keys}/assets.bin" ${pak_inputs} "${SAT_GAME_PROJECT}/Scene.sat"
        "${CMAKE_CURRENT_BINARY_DIR}/Pak.manifest"
    COMMENT "Compiling Lua and encrypting split project archives" VERBATIM)
add_custom_target(SatGamePak DEPENDS "${pak_output}" "${pak_output}.parts")
add_custom_target(SatGamePakStage
    COMMAND ${CMAKE_COMMAND} "-DSAT_PAK=${pak_output}" "-DSAT_DEST=$<TARGET_FILE_DIR:GamePlayer>"
        -P "${PROJECT_SOURCE_DIR}/cmake/StagePak.cmake"
    DEPENDS SatGamePak VERBATIM)
add_dependencies(GamePlayer SatGamePakStage)
target_link_libraries(GamePlayer PRIVATE SatRuntime SatPak)
install(CODE "
    set(SAT_PAK \"${pak_output}\")
    set(SAT_DEST \"\${CMAKE_INSTALL_PREFIX}\")
    include(\"${PROJECT_SOURCE_DIR}/cmake/StagePak.cmake\")
")

# MSBuild/VS often skips relinking GamePlayer when runtime libraries change.
# Refresh a stub .cpp whenever the lib is newer so GamePlayer recompiles and relinks.
set(_sat_relink_stub "${CMAKE_CURRENT_BINARY_DIR}/satengine_relink_stub.cpp")
if(NOT EXISTS "${_sat_relink_stub}")
    file(WRITE "${_sat_relink_stub}" "/* Forces GamePlayer relink when runtime libraries change. */\n")
endif()
add_custom_command(
    OUTPUT "${_sat_relink_stub}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_sat_relink_stub}"
    DEPENDS "$<TARGET_FILE:SatAssets>" "$<TARGET_FILE:SatAudio>" "$<TARGET_FILE:SatScripting>" "$<TARGET_FILE:SatLua>" "$<TARGET_FILE:SatScene>" "$<TARGET_FILE:SatRuntime>" "$<TARGET_FILE:SatPak>"
    COMMENT "runtime libraries changed; refreshing GamePlayer relink stub"
    VERBATIM
)
target_sources(GamePlayer PRIVATE "${_sat_relink_stub}")

set_target_properties(GamePlayer PROPERTIES
    OUTPUT_NAME "${_sat_game_name}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:GamePlayer>"
    VS_GLOBAL_DisableFastUpToDateCheck true
)

# Ship the executable and its matching pak together.
install(TARGETS GamePlayer RUNTIME DESTINATION .)

target_compile_definitions(GamePlayer PRIVATE SAT_GAME_PAK_NAME="${_sat_game_name}.pak")

if(SAT_SHADER_HOT_RELOAD)
    add_custom_command(TARGET GamePlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DCONFIG=$<CONFIG>" "-DSLANG_DLL=${SAT_SLANG_DLL}"
            "-DDEST=$<TARGET_FILE_DIR:GamePlayer>" -P "${PROJECT_SOURCE_DIR}/scripts/StageSlang.cmake"
        VERBATIM)
endif()
