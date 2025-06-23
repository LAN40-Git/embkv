cmake_minimum_required(VERSION 3.31)
project(embkv)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
    embkv/src/main.cpp
)

add_executable(main
        ${SOURCE_FILES}
)
target_include_directories(main PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
    $<INSTALL_INTERFACE:include/embkv>
)
target_link_libraries(main PUBLIC

)