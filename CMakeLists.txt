cmake_minimum_required(VERSION 3.28)
if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "必须指定ARM工具链文件！")
endif()

project(embkv)
cmake_policy(SET CMP0167 NEW)

# ==================== 全局配置 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXE_LINKER_FLAGS "-static")      # 强制完全静态链接
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")      # 只查找静态库
set(BUILD_SHARED_LIBS OFF)                # 禁止生成动态库

# ==================== 交叉编译设置 ====================
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ARM")
    # 禁止查找宿主机路径
    set(Boost_NO_SYSTEM_PATHS ON)
    set(FMT_NO_SYSTEM_PATHS ON)
    set(SQLite3_NO_SYSTEM_PATHS ON)
endif()

# ==================== Boost静态库配置 ====================
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(BOOST_ROOT "/home/hujinhao/arm-boost")
set(Boost_NO_SYSTEM_PATHS ON)

# 直接指定库路径（避免find_library失败）
set(Boost_INCLUDE_DIR "${BOOST_ROOT}/include")
set(Boost_LIBRARY_DIR "${BOOST_ROOT}/lib")

# 硬编码库文件路径
set(Boost_SYSTEM_LIBRARY "${Boost_LIBRARY_DIR}/libboost_system.a")
set(Boost_THREAD_LIBRARY "${Boost_LIBRARY_DIR}/libboost_thread.a")

# 验证库文件是否存在
foreach(LIB ${Boost_SYSTEM_LIBRARY} ${Boost_THREAD_LIBRARY})
    if(NOT EXISTS ${LIB})
        message(FATAL_ERROR "Boost库未找到: ${LIB}")
    endif()
endforeach()

# ==================== 其他依赖库配置 ====================
# fmt
find_package(fmt REQUIRED)
if(TARGET fmt::fmt)
    set(FMT_LIBRARY fmt::fmt)
else()
    find_library(FMT_LIBRARY fmt PATHS ${FMT_ROOT}/lib NO_DEFAULT_PATH REQUIRED)
endif()

# SQLite3
set(SQLite3_LIBRARY "/usr/arm-linux-gnueabihf/lib/libsqlite3.a")

# 验证静态库是否存在
if(NOT EXISTS ${SQLite3_LIBRARY})
    message(FATAL_ERROR "SQLite3静态库不存在: ${SQLite3_LIBRARY}")
endif()

# Protobuf
set(Protobuf_USE_STATIC_LIBS ON)
set(Protobuf_ROOT "/usr/local/protobuf-arm")
set(Protobuf_LIBRARY "${Protobuf_ROOT}/lib/libprotobuf.a")

# libev
find_library(LIBEV_LIBRARY
    NAMES libev.a ev
    PATHS /usr/arm-linux-gnueabihf/lib /usr/local/arm-libev/lib
    NO_DEFAULT_PATH
    REQUIRED
)
find_path(LIBEV_INCLUDE_DIR ev.h
    PATHS /usr/arm-linux-gnueabihf/include /usr/local/arm-libev/include
    REQUIRED
)

# nlohmann_json
find_package(nlohmann_json 3.5 REQUIRED)

# ==================== 源文件配置 ====================
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
)

set(PROTO_SRCS embkv/src/proto/rpc.pb.cc)
set(PROTO_HDRS embkv/src/proto/rpc.pb.h)

# ==================== 可执行文件 ====================
add_executable(main ${SOURCE_FILES} ${PROTO_SRCS})

target_include_directories(main PRIVATE
    ${LIBEV_INCLUDE_DIR}
    ${Boost_INCLUDE_DIR}
    ${Protobuf_ROOT}/include
    ${nlohmann_json_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/embkv/include>
)

target_link_options(main PRIVATE
    "LINKER:--no-as-needed"
    "LINKER:-ldl"
    "LINKER:-lresolv"
)

target_link_libraries(main PRIVATE
    ${LIBEV_LIBRARY}
    ${Protobuf_LIBRARY}
    ${Boost_SYSTEM_LIBRARY}
    ${Boost_THREAD_LIBRARY}
    ${FMT_LIBRARY}
    ${SQLite3_LIBRARY}
    -static-libstdc++
    -static-libgcc
    -lpthread
    -ldl
    nlohmann_json::nlohmann_json
)

# ==================== 验证 ====================
if(NOT EXISTS "${Boost_SYSTEM_LIBRARY}")
    message(FATAL_ERROR "Boost静态库未找到: ${Boost_SYSTEM_LIBRARY}")
endif()

message(STATUS "
=============================================
配置摘要:
* Boost: ${Boost_SYSTEM_LIBRARY}
* Protobuf: ${Protobuf_LIBRARY}
* libev: ${LIBEV_LIBRARY}
* SQLite3: ${SQLite3_LIBRARY}
* 完全静态链接: YES
=============================================
")