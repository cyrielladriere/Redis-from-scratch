cmake_minimum_required(VERSION 3.25)
project(Redis LANGUAGES CXX)

# Add include directory
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add the src directory
add_subdirectory(src)