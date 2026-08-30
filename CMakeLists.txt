cmake_minimum_required(VERSION 3.28)

project(HC32F460xE_MCUBoot LANGUAGES C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BOOT_BUILD_HOST_TESTS "Build host-only validation tests" OFF)

include(cmake/MemoryMap.cmake)
include(cmake/ProductIdentity.cmake)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/Config")
configure_file(
    Config/Memory/boot_memory_map.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Config/boot_memory_map.h"
    @ONLY
)
configure_file(
    Config/Product/product_identity.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Config/product_identity.h"
    @ONLY
)

if(BOOT_BUILD_HOST_TESTS)
    enable_testing()
    add_subdirectory(Tests)
    return()
endif()

include(cmake/ProjectOptions.cmake)
include(cmake/FirmwareArtifacts.cmake)
include(cmake/Imgtool.cmake)

add_subdirectory(Drivers)
add_subdirectory(Platform)
add_subdirectory(components)
add_subdirectory(Boot)
add_subdirectory(App)
