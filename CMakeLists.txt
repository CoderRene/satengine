cmake_minimum_required(VERSION 3.24)

project(SatEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>
    )
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(SAT_BUILD_GAME_PLAYER "Build the standalone game player" ON)
option(SAT_BUILD_EDITOR "Build the editor shell" ON)
include(CTest)

if((SAT_BUILD_GAME_PLAYER OR SAT_BUILD_EDITOR) AND NOT WIN32)
    message(FATAL_ERROR "The D3D11 runtime requires Windows. For portable scene tests, set SAT_BUILD_GAME_PLAYER=OFF and SAT_BUILD_EDITOR=OFF.")
endif()

add_subdirectory(Engine)
if(SAT_BUILD_GAME_PLAYER)
    add_subdirectory(GamePlayer)
endif()
if(SAT_BUILD_EDITOR)
    add_subdirectory(Editor)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Editor)
endif()
if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()
