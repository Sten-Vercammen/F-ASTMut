cmake_minimum_required(VERSION 2.8.9)
project(mini)

include_directories(include)

# magically find all cpp files in src
file(GLOB SOURCES "src/*.cpp")

add_executable(test ${SOURCES})
