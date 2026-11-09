add_executable(Game
    src/main.cpp
)

if(WIN32)
    target_sources(Game PRIVATE SatEngine.rc)
endif()

target_link_libraries(Game PRIVATE SatEngine)

# MSBuild/VS often skips relinking Game when only SatEngine.lib changes.
# Refresh a stub .cpp whenever the lib is newer so Game recompiles and relinks.
set(_sat_relink_stub "${CMAKE_CURRENT_BINARY_DIR}/satengine_relink_stub.cpp")
if(NOT EXISTS "${_sat_relink_stub}")
    file(WRITE "${_sat_relink_stub}" "/* Forces Game relink when SatEngine.lib changes. */\n")
endif()
add_custom_command(
    OUTPUT "${_sat_relink_stub}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_sat_relink_stub}"
    DEPENDS "$<TARGET_FILE:SatEngine>"
    COMMENT "SatEngine.lib changed; refreshing Game relink stub"
    VERBATIM
)
target_sources(Game PRIVATE "${_sat_relink_stub}")

# Keep runtime fonts/icons next to the executable for packaged / Debug-folder runs.
add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/Fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Game>/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Engine/Assets/Fonts/JetBrainsMono.ttf"
        "$<TARGET_FILE_DIR:Game>/Fonts/JetBrainsMono.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Engine/Assets/SatEngine_ic.png"
        "$<TARGET_FILE_DIR:Game>/SatEngine_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Engine/Assets/Icons/folder_ic.png"
        "$<TARGET_FILE_DIR:Game>/Icons/folder_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Engine/Assets/Icons/return_ic.png"
        "$<TARGET_FILE_DIR:Game>/Icons/return_ic.png"
    COMMENT "Copy engine fonts and icons next to Game"
    VERBATIM
)

set_target_properties(Game PROPERTIES
    OUTPUT_NAME "SatEngine"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Game>"
    VS_GLOBAL_DisableFastUpToDateCheck true
)
