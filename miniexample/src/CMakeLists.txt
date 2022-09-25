add_library(Mini mini.cpp ${HEADER_FILES})

add_executable(mini main.cpp)
target_link_libraries(mini Mini)
