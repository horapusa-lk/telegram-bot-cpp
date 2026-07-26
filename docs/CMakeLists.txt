find_package(Doxygen REQUIRED)

configure_file(Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

# `cmake --build build --target docs` renders the API reference; the Doxyfile
# treats warnings (including undocumented public symbols) as errors.
add_custom_target(docs
    COMMAND Doxygen::doxygen ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating Doxygen HTML (warnings are errors)"
    VERBATIM)
