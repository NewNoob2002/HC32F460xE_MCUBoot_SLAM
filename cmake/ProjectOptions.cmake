set(CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

add_library(hc32_project_options INTERFACE)
target_include_directories(hc32_project_options INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Config
    ${PROJECT_SOURCE_DIR}/Config/Debug
)
target_compile_definitions(hc32_project_options INTERFACE
    HC32F460
    USE_DDL_DRIVER
    $<$<CONFIG:Debug>:BSP_DEBUG=1>
)
target_compile_options(hc32_project_options INTERFACE
    ${CPU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
    # Keep __FILE__ strings independent of the checkout path; Boot has only a small margin in its 64 KiB region.
    -fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-Os -g0>
)
target_link_options(hc32_project_options INTERFACE
    ${CPU_FLAGS}
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

# Parses MAJOR.MINOR.REVISION[+BUILD] into <name>_{MAJOR,MINOR,REVISION,BUILD}_VALUE, checked against Protocol V1 widths.
function(hc32_parse_version name value)
    if(NOT value MATCHES "^([0-9]+)[.]([0-9]+)[.]([0-9]+)([+]([0-9]+))?$")
        message(FATAL_ERROR "${name} must be MAJOR.MINOR.REVISION[+BUILD]")
    endif()
    set(build ${CMAKE_MATCH_5})
    if("${build}" STREQUAL "")
        set(build 0)
    endif()
    if(CMAKE_MATCH_1 GREATER 255 OR CMAKE_MATCH_2 GREATER 255 OR CMAKE_MATCH_3 GREATER 65535
        OR build GREATER 4294967295)
        message(FATAL_ERROR "${name} fields exceed Protocol V1 widths")
    endif()
    set(${name}_MAJOR_VALUE ${CMAKE_MATCH_1} PARENT_SCOPE)
    set(${name}_MINOR_VALUE ${CMAKE_MATCH_2} PARENT_SCOPE)
    set(${name}_REVISION_VALUE ${CMAKE_MATCH_3} PARENT_SCOPE)
    set(${name}_BUILD_VALUE ${build} PARENT_SCOPE)
endfunction()
