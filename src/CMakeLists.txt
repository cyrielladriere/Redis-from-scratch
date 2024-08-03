# Make sure both executables are saved in /build instead of /build/src/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(redis_server server.cpp)
add_executable(redis_client client.cpp)