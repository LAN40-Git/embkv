cmake_minimum_required(VERSION 3.28)
if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "必须指定ARM工具链文件！")
endif()
project(embkv)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 交叉编译时禁用宿主机的包查找（避免误用 x86_64 库）
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for ARM")
    set(Boost_NO_SYSTEM_PATHS ON)  # 禁止从系统路径查找 Boost
    set(FMT_NO_SYSTEM_PATHS ON)     # 禁止从系统路径查找 fmt
endif()

set(Boost_NO_SYSTEM_PATHS ON)
set(BOOST_ROOT "/home/hujinhao/arm-boost")
find_package(Boost 1.83.0 REQUIRED COMPONENTS system thread)

# 其他依赖库（fmt、json、libev、SQLite3、Protobuf）
set(fmt_DIR "/usr/lib/arm-linux-gnueabihf/cmake/fmt")
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
        protobuf::libprotobuf
)

if(NOT LIBEV_INCLUDE_DIR OR NOT LIBEV_LIBRARY)
    message(FATAL_ERROR "libev not found for ARM!")
endif()