cmake_minimum_required(VERSION 3.31)
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.83.0 REQUIRED COMPONENTS thread)

set(SOURCE_FILES
        embkv/src/main.cpp
        embkv/src/raft/node.cpp
        embkv/src/raft/transport/transport.cpp
        embkv/src/raft/transport/session_manager.cpp
        embkv/src/raft/peer/pipeline.cpp
        embkv/src/raft/log.cpp
        embkv/src/socket/socket.cpp
        embkv/src/common/util/fd.cpp
        embkv/src/client/client.cpp
        embkv/include/client/client.h
)

# fmt
find_package(fmt REQUIRED)

# json
find_package(nlohmann_json 3.5 REQUIRED)

# libev
find_path(LIBEV_INCLUDE_DIR ev.h
    PATHS /usr/include /usr/local/include
)
find_library(LIBEV_LIBRARY ev
    PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# sqlite3
find_package(SQLite3 REQUIRED)

# protobuf
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ./rpc.proto)
set_source_files_properties(${PROTO_HDRS} PROPERTIES GENERATED TRUE)

if(LIBEV_INCLUDE_DIR AND LIBEV_LIBRARY)
    add_executable(main
            ${SOURCE_FILES}
            ${PROTO_SRCS}
    )
    target_include_directories(main PRIVATE
            ${LIBEV_INCLUDE_DIR}
            ${PROTO_HDRS}
            ${Boost_INCLUDE_DIRS}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
    )
    target_link_libraries(main PRIVATE
            ${LIBEV_LIBRARY}
            ${PROTOBUF_LIBRARIES}
            ${Boost_LIBRARIES}
            Boost::thread
            fmt::fmt
            SQLite::SQLite3
            nlohmann_json::nlohmann_json
    )
else()
    message(FATAL_ERROR "libev not found!")
endif()