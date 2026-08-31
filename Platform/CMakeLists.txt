add_library(hc32_platform STATIC
    HC32F460/Src/boot_handover.c
    HC32F460/Src/bsp_clock.c
    HC32F460/Src/bsp_external_watchdog.c
    HC32F460/Src/bsp_flash.c
    HC32F460/Src/bsp_log_uart.c
    HC32F460/Src/bsp_panic.c
    HC32F460/Src/bsp_reset.c
    HC32F460/Src/bsp_timebase.c
    HC32F460/Src/bsp_usb.c
    HC32F460/Src/bsp_write_protection.c
    HC32F460/Src/syscalls.c
)
target_include_directories(hc32_platform PUBLIC HC32F460/Inc)
target_include_directories(hc32_platform PRIVATE
    HC32F460/Config
    ${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/boot/bootutil/include
    ${CMAKE_CURRENT_SOURCE_DIR}/HC32F460/Ports/mcuboot/Inc
)
target_link_libraries(hc32_platform PUBLIC hc32_ll)

add_subdirectory(HC32F460/Ports/mcuboot)
add_subdirectory(HC32F460/Ports/flashdb)
add_subdirectory(HC32F460/Ports/easylogger EXCLUDE_FROM_ALL)

add_library(hc32_usb_device_port STATIC
    HC32F460/Src/usb_dc_hc32f460.c
)
target_include_directories(hc32_usb_device_port SYSTEM PRIVATE
    HC32F460/Config
    ${PROJECT_SOURCE_DIR}/components/cherryusb/common
    ${PROJECT_SOURCE_DIR}/components/cherryusb/core
    ${PROJECT_SOURCE_DIR}/Config/Application
)
target_link_libraries(hc32_usb_device_port PUBLIC hc32_platform)

add_library(hc32_boot_fault OBJECT
    HC32F460/Src/boot_fault.c
)
target_include_directories(hc32_boot_fault PRIVATE HC32F460/Inc)
target_link_libraries(hc32_boot_fault PRIVATE hc32_device)
