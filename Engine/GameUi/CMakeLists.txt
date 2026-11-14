include(FetchContent)

set(BUILD_SHARED_LIBS OFF)
foreach(dependency ZLIB PNG HARFBUZZ BZIP2 BROTLI)
    set(FT_DISABLE_${dependency} ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(Freetype
    URL https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-13-3
    URL_HASH SHA256=bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR satengine-unused
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Freetype)
add_subdirectory("${freetype_SOURCE_DIR}" "${freetype_BINARY_DIR}" EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)

set(RMLUI_FONT_ENGINE freetype)
set(RMLUI_MATRIX_ROW_MAJOR OFF)
set(RMLUI_CUSTOM_RTTI OFF)
set(RMLUI_SAMPLES OFF)
set(RMLUI_LUA_BINDINGS OFF)
set(RMLUI_TESTS OFF)
FetchContent_Declare(RmlUi
    URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.1
    URL_HASH SHA256=b6088bf31858d31bfe657caecf49fd12d5a34f9a37fa1c3061757410c4eb0089
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR satengine-unused
)
FetchContent_MakeAvailable(RmlUi)
add_subdirectory("${rmlui_SOURCE_DIR}" "${rmlui_BINARY_DIR}" EXCLUDE_FROM_ALL)

add_library(SatGameUi STATIC
    src/GameUi.cpp
    src/module/UiFileInterface.cpp
    src/module/UiSystemInterface.cpp
    src/module/UiEventQueue.cpp
)
target_include_directories(SatGameUi
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(SatGameUi PUBLIC cxx_std_20)
target_link_libraries(SatGameUi PUBLIC SatScene PRIVATE SatInput RmlUi::Core)
