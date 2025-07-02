x86
```cmake
cmake_minimum_required(VERSION 3.28)
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.83.0 REQUIRED COMPONENTS thread system)

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
            Boost::system
            Boost::thread
            fmt::fmt
            SQLite::SQLite3
            nlohmann_json::nlohmann_json
    )
else()
    message(FATAL_ERROR "libev not found!")
endif()
```


arm
```cmake
cmake_minimum_required(VERSION 3.28)
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 交叉编译时禁用宿主机的包查找（避免误用 x86_64 库）
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ARM")
    set(Boost_NO_SYSTEM_PATHS ON)  # 禁止从系统路径查找 Boost
    set(FMT_NO_SYSTEM_PATHS ON)     # 禁止从系统路径查找 fmt
endif()

# 设置 Boost 路径（交叉编译时需要手动指定）
set(BOOST_ROOT "/usr/local/arm-boost")
set(Boost_NO_SYSTEM_PATHS ON)
find_package(Boost 1.83.0 REQUIRED COMPONENTS thread system)

# 其他依赖库（fmt、json、libev、SQLite3、Protobuf）
find_package(fmt REQUIRED)
find_package(nlohmann_json 3.5 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Protobuf REQUIRED)

# libev：手动指定交叉编译的路径
find_path(LIBEV_INCLUDE_DIR ev.h
    PATHS /usr/arm-linux-gnueabihf/include /usr/local/arm-libev/include
)
find_library(LIBEV_LIBRARY ev
    PATHS /usr/arm-linux-gnueabihf/lib /usr/local/arm-libev/lib
)

# Protobuf 代码生成
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ./rpc.proto)
set_source_files_properties(${PROTO_HDRS} PROPERTIES GENERATED TRUE)

# 可执行文件
add_executable(main
    ${SOURCE_FILES}
    ${PROTO_SRCS}
)

# 头文件路径
target_include_directories(main PRIVATE
    ${LIBEV_INCLUDE_DIR}
    ${PROTO_HDRS}
    ${Boost_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
)

# 链接库
target_link_libraries(main PRIVATE
    ${LIBEV_LIBRARY}
    Boost::system
    Boost::thread
    fmt::fmt
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
)

if(NOT LIBEV_INCLUDE_DIR OR NOT LIBEV_LIBRARY)
    message(FATAL_ERROR "libev not found for ARM!")
endif()
```
