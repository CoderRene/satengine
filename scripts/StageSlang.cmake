if(NOT CONFIG STREQUAL "Release")
    get_filename_component(name "${SLANG_DLL}" NAME)
    file(COPY_FILE "${SLANG_DLL}" "${DEST}/${name}" ONLY_IF_DIFFERENT)
endif()
