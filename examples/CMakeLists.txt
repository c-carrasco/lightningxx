add_executable (lightning_hello hello.cxx)
target_link_libraries (lightning_hello PRIVATE lightning)

add_executable (lightning_middleware middleware.cxx)
target_link_libraries (lightning_middleware PRIVATE lightning)

add_executable (lightning_router router.cxx)
target_link_libraries (lightning_router PRIVATE lightning)

add_executable (lightning_rest rest.cxx)
target_link_libraries (lightning_rest PRIVATE lightning)
add_executable (lightning_async async.cxx)
target_link_libraries (lightning_async PRIVATE lightning)
