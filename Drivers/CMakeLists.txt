set(HC32_DEVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Device/HDSC/hc32f4xx)
set(HC32_LL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/hc32f4xx_ll_drivers)

add_library(hc32_device INTERFACE)
target_include_directories(hc32_device INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS/Include
    ${HC32_DEVICE_DIR}/Include
    ${HC32_LL_DIR}/inc
    ${PROJECT_SOURCE_DIR}/Platform/HC32F460/Config
)
target_link_libraries(hc32_device INTERFACE hc32_project_options)

add_library(hc32_ll STATIC
    ${HC32_LL_DIR}/src/hc32_ll.c
    ${HC32_LL_DIR}/src/hc32_ll_clk.c
    ${HC32_LL_DIR}/src/hc32_ll_efm.c
    ${HC32_LL_DIR}/src/hc32_ll_fcg.c
    ${HC32_LL_DIR}/src/hc32_ll_gpio.c
    ${HC32_LL_DIR}/src/hc32_ll_i2c.c
    ${HC32_LL_DIR}/src/hc32_ll_interrupts.c
    ${HC32_LL_DIR}/src/hc32_ll_pwc.c
    ${HC32_LL_DIR}/src/hc32_ll_sram.c
    ${HC32_LL_DIR}/src/hc32_ll_usart.c
    ${HC32_LL_DIR}/src/hc32_ll_utility.c
    ${HC32_LL_DIR}/src/hc32_ll_usb.c
)
target_link_libraries(hc32_ll PUBLIC hc32_device)

add_library(hc32_startup_boot OBJECT
    ${HC32_DEVICE_DIR}/Source/GCC/startup_hc32f460.S
    ${HC32_DEVICE_DIR}/Source/system_hc32f460.c
    ${HC32_LL_DIR}/src/hc32_ll_icg.c
)
target_compile_definitions(hc32_startup_boot PRIVATE VECT_TAB_OFFSET=0x00000000UL)
target_link_libraries(hc32_startup_boot PRIVATE hc32_device)

add_library(hc32_startup_app OBJECT
    ${HC32_DEVICE_DIR}/Source/GCC/startup_hc32f460.S
    ${HC32_DEVICE_DIR}/Source/system_hc32f460.c
)
target_compile_definitions(hc32_startup_app PRIVATE VECT_TAB_OFFSET=0x00010200UL)
target_link_libraries(hc32_startup_app PRIVATE hc32_device)
