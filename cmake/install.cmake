include (CMakePackageConfigHelpers)
set (LIGHTNING_PACKAGE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.0")
set (LIGHTNING_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/lightning")

# The pinned header-only logger is part of our public API. Export it with its
# headers and license, without requiring a second dependency manager at use time.
set_property (TARGET cxxlogger PROPERTY INTERFACE_INCLUDE_DIRECTORIES
  "$<BUILD_INTERFACE:${cxxlogger_SOURCE_DIR}/src/include>;$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
install (TARGETS lightning cxxlogger EXPORT lightningTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install (DIRECTORY src/include/lightning DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install (DIRECTORY "${cxxlogger_SOURCE_DIR}/src/include/cxxlog" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install (FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/lightning)
install (FILES "${cxxlogger_SOURCE_DIR}/LICENSE"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/lightning RENAME CxxLogger-LICENSE)
configure_package_config_file (cmake/lightningConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/lightningConfig.cmake"
  INSTALL_DESTINATION ${LIGHTNING_CMAKE_INSTALL_DIR})
write_basic_package_version_file ("${CMAKE_CURRENT_BINARY_DIR}/lightningConfigVersion.cmake"
  VERSION ${LIGHTNING_PACKAGE_VERSION} COMPATIBILITY SameMinorVersion)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/lightningConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/lightningConfigVersion.cmake"
  DESTINATION ${LIGHTNING_CMAKE_INSTALL_DIR})
install (EXPORT lightningTargets NAMESPACE lightning:: DESTINATION ${LIGHTNING_CMAKE_INSTALL_DIR})

# Verify distributable builds; instrumentation runtimes are deliberately not
# exported. Sanitizer and coverage configurations exercise the regular suite.
if (BUILD_TESTING AND NOT ENABLE_ASAN AND NOT ENABLE_UBSAN AND NOT ENABLE_TSAN
    AND NOT LIGHTNING_ENABLE_COVERAGE)
  configure_file (cmake/test_install.cmake.in test_install.cmake @ONLY)
  add_test (NAME installed_consumer COMMAND ${CMAKE_COMMAND}
    -DTEST_CONFIG=$<CONFIG> -P "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake")
  set_tests_properties (installed_consumer PROPERTIES TIMEOUT 120)
endif()
