cmake_minimum_required(VERSION 3.31)
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 先定义源文件
set(SOURCE_FILES
    embkv/src/main.cpp
)

# fmt
find_package(fmt REQUIRED)

# libev
find_path(LIBEV_INCLUDE_DIR ev.h
    PATHS /usr/include /usr/local/include
)
find_library(LIBEV_LIBRARY ev
    PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu
)

if(LIBEV_INCLUDE_DIR AND LIBEV_LIBRARY)
    message(STATUS "Found libev: ${LIBEV_LIBRARY}")

    add_executable(main ${SOURCE_FILES})
    target_include_directories(main PRIVATE
        ${LIBEV_INCLUDE_DIR}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
    )
    target_link_libraries(main PRIVATE
        ${LIBEV_LIBRARY}
        fmt::fmt
    )
else()
    message(FATAL_ERROR "libev not found!")
endif()