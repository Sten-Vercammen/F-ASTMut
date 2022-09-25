cmake_minimum_required(VERSION 2.8.9)
project(mini)

include_directories(include)
add_subdirectory(include)

add_subdirectory(src)

enable_testing()

add_subdirectory(test)

