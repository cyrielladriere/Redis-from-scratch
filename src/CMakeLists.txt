add_library(redis_utils STATIC utils.cpp)

add_executable(redis_server server.cpp)
add_executable(redis_client client.cpp)

target_link_libraries(redis_server PRIVATE redis_utils)
target_link_libraries(redis_client PRIVATE redis_utils)