file (GLOB CXX_FILES FILES *.cxx)

add_library (lightning STATIC ${CXX_FILES})

target_include_directories (lightning
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
)

target_link_libraries (lightning asio::asio http_parser::http_parser cxxlogger)