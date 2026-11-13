add_executable(Editor
    src/main.cpp
    src/EditorApplication.cpp
)

if(WIN32)
    target_sources(Editor PRIVATE SatEngine.rc)
endif()

target_link_libraries(Editor PRIVATE SatEditor)
add_dependencies(Editor SatShaders SatEditorShaders)

# MSBuild/VS often skips relinking Editor when engine libraries change.
# Refresh a stub .cpp whenever the lib is newer so Editor recompiles and relinks.
set(_sat_relink_stub "${CMAKE_CURRENT_BINARY_DIR}/satengine_relink_stub.cpp")
if(NOT EXISTS "${_sat_relink_stub}")
    file(WRITE "${_sat_relink_stub}" "/* Forces Editor relink when engine libraries change. */\n")
endif()
add_custom_command(
    OUTPUT "${_sat_relink_stub}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_sat_relink_stub}"
    DEPENDS ${SAT_EDITOR_SHADER_OUTPUTS} ${SAT_SHADER_OUTPUTS} "$<TARGET_FILE:SatAssets>" "$<TARGET_FILE:SatScripting>" "$<TARGET_FILE:SatLua>" "$<TARGET_FILE:SatScene>" "$<TARGET_FILE:SatRuntime>" "$<TARGET_FILE:SatEditor>"
    COMMENT "Engine library changed; refreshing Editor relink stub"
    VERBATIM
)
target_sources(Editor PRIVATE "${_sat_relink_stub}")

# Keep runtime fonts/icons next to the executable for packaged / Debug-folder runs.
add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/Shaders" "$<TARGET_FILE_DIR:Editor>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/EditorShaders" "$<TARGET_FILE_DIR:Editor>/EditorShaders/Shaders"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:Editor>/LuaDefs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/Engine/Scripting/LuaDefs"
        "$<TARGET_FILE_DIR:Editor>/LuaDefs"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Editor>/Fonts"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Editor>/Icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Fonts/JetBrainsMono.ttf"
        "$<TARGET_FILE_DIR:Editor>/Fonts/JetBrainsMono.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/SatEngine_ic.png"
        "$<TARGET_FILE_DIR:Editor>/SatEngine_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/folder_ic.png"
        "$<TARGET_FILE_DIR:Editor>/Icons/folder_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/video_camera_ic.png"
        "$<TARGET_FILE_DIR:Editor>/Icons/video_camera_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/return_ic.png"
        "$<TARGET_FILE_DIR:Editor>/Icons/return_ic.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/light_ic.png"
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/spotlight_ic.png"
        "${PROJECT_SOURCE_DIR}/Engine/Assets/Icons/sun_ic.png"
        "$<TARGET_FILE_DIR:Editor>/Icons/"
    COMMENT "Copy engine fonts, icons, and LuaDefs next to Editor"
    VERBATIM
)

set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "SatEngine"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Editor>"
    VS_GLOBAL_DisableFastUpToDateCheck true
)

if(TARGET SatLuaDefs)
    add_dependencies(Editor SatLuaDefs)
endif()

if(SAT_SHADER_HOT_RELOAD)
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DCONFIG=$<CONFIG>" "-DSLANG_DLL=${SAT_SLANG_DLL}"
            "-DDEST=$<TARGET_FILE_DIR:Editor>" -P "${PROJECT_SOURCE_DIR}/scripts/StageSlang.cmake"
        VERBATIM)
endif()
