include(FetchContent)
# Populate the sources. SatAudio owns the single implementation translation unit.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.23
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR satengine-unused
)
FetchContent_MakeAvailable(miniaudio)

add_library(SatAudio STATIC
    src/AudioSystem.cpp
    src/module/MiniaudioImpl.cpp
    src/module/AudioClipCache.cpp
    src/module/VoicePool.cpp
)
target_include_directories(SatAudio
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/Engine/Core/src"
)
target_include_directories(SatAudio SYSTEM PRIVATE "${miniaudio_SOURCE_DIR}" "${miniaudio_SOURCE_DIR}/extras")
target_compile_features(SatAudio PUBLIC cxx_std_20)
target_compile_definitions(SatAudio PRIVATE MA_NO_ENCODING MA_NO_GENERATION)
target_link_libraries(SatAudio PUBLIC SatScene PRIVATE SatPhysics)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(SatAudio PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
