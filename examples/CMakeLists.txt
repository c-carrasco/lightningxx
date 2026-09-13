add_executable (lightning_hello hello.cxx)
target_link_libraries (lightning_hello PRIVATE lightning)

add_executable (lightning_middleware middleware.cxx)
target_link_libraries (lightning_middleware PRIVATE lightning)
