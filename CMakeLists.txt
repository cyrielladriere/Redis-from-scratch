cmake_minimum_required(VERSION 3.25)
project(Redis LANGUAGES CXX)

add_executable(redis_server server.cpp)
add_executable(redis_client client.cpp)