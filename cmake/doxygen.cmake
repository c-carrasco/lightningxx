find_package (Doxygen)

if (DOXYGEN_FOUND)
  configure_file (${CMAKE_SOURCE_DIR}/doc/doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/doxyfile.cfg @ONLY)

  add_custom_target (documentation
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/doxyfile.cfg
    COMMENT "Generating documentation with doxygen"
    VERBATIM
  )
else()
  message (WARNING "Doxygen not found")
endif()