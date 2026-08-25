cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C CXX ASM)
# 设置 C 和 C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "")
message(STATUS "配置 SEGGER 中间件...")

file(GLOB_RECURSE SEGGER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

add_library(SEGGER STATIC ${SEGGER_SOURCES})
target_include_directories(SEGGER PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/
)
target_link_libraries(SEGGER PUBLIC stm32cubemx)

message(STATUS "配置 SEGGER 完成")