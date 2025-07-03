
set(CMAKE_SYSTEM_PROCESSOR arm)

# 指定编译器
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(SQLite3_INCLUDE_DIR "/usr/arm-linux-gnueabihf/include")
set(SQLite3_LIBRARY "/usr/arm-linux-gnueabihf/lib/libsqlite3.so")

# 设置查找路径
set(CMAKE_FIND_ROOT_PATH
    /usr/arm-linux-gnueabihf
    /home/${USER}/arm-libs
)

# 禁用主机路径查找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 抑制警告
add_compile_options(-Wno-psabi)