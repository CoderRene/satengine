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
include("${PROJECT_SOURCE_DIR}/cmake/EmbedLuaProject.cmake")
sat_embed_lua_project(SatGameScripts "${SAT_GAME_PROJECT}")
target_link_libraries(GamePlayer PRIVATE SatRuntime SatGameScripts)

# MSBuild/VS often skips relinking GamePlayer when runtime libraries change.
# Refresh a stub .cpp whenever the lib is newer so GamePlayer recompiles and relinks.
set(_sat_relink_stub "${CMAKE_CURRENT_BINARY_DIR}/satengine_relink_stub.cpp")
if(NOT EXISTS "${_sat_relink_stub}")
    file(WRITE "${_sat_relink_stub}" "/* Forces GamePlayer relink when runtime libraries change. */\n")
endif()
add_custom_command(
    OUTPUT "${_sat_relink_stub}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_sat_relink_stub}"
    DEPENDS "$<TARGET_FILE:SatGameScripts>" "$<TARGET_FILE:SatScripting>" "$<TARGET_FILE:SatLua>" "$<TARGET_FILE:SatScene>" "$<TARGET_FILE:SatRuntime>"
    COMMENT "runtime libraries changed; refreshing GamePlayer relink stub"
    VERBATIM
)
target_sources(GamePlayer PRIVATE "${_sat_relink_stub}")

set_target_properties(GamePlayer PROPERTIES
    OUTPUT_NAME "${_sat_game_name}"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:GamePlayer>"
    VS_GLOBAL_DisableFastUpToDateCheck true
)

# Lua project code is embedded; no project folder or editor assets are needed at runtime.
install(TARGETS GamePlayer RUNTIME DESTINATION .)
