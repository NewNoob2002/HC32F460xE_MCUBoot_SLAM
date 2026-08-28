set(CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
)

add_library(hc32_project_options INTERFACE)
target_include_directories(hc32_project_options INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated/Config
)
target_compile_definitions(hc32_project_options INTERFACE
    HC32F460
    USE_DDL_DRIVER
    $<$<CONFIG:Debug>:HC32_DEBUG_LOG=1>
)
target_compile_options(hc32_project_options INTERFACE
    ${CPU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror=return-type
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
