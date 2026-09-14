file (GLOB CXX_FILES FILES *.cxx)

add_library (lightning STATIC ${CXX_FILES})
add_library (lightning::lightning ALIAS lightning)
target_compile_features (lightning PUBLIC cxx_std_20)

target_include_directories (lightning
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries (lightning PUBLIC asio::asio llhttp::llhttp cxxlogger nlohmann_json::nlohmann_json Threads::Threads)
# Match the standard library used to build the archive, including by consumers.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options (lightning PUBLIC -stdlib=libc++)
  target_link_options (lightning PUBLIC -stdlib=libc++)
endif()
