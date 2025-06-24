cmake_minimum_required(VERSION 3.31)
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_FILES
        embkv/src/main.cpp
        embkv/src/raft/raft_node.cpp
        embkv/src/raft/transport.cpp
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

# protobuf
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ./rpc.proto)

if(LIBEV_INCLUDE_DIR AND LIBEV_LIBRARY)
    add_executable(main
            ${SOURCE_FILES}
            ${PROTO_SRCS}
    )
    target_include_directories(main PRIVATE
            ${LIBEV_INCLUDE_DIR}
            ${PROTO_HDRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
    )
    target_link_libraries(main PRIVATE
            ${LIBEV_LIBRARY}
            ${PROTOBUF_LIBRARIES}
            fmt::fmt
    )
else()
    message(FATAL_ERROR "libev not found!")
endif()