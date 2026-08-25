add_library(hc32_platform STATIC
    HC32F460/Src/boot_handover.c
    HC32F460/Src/bsp_clock.c
    HC32F460/Src/bsp_flash.c
)
target_include_directories(hc32_platform PUBLIC HC32F460/Inc)
target_include_directories(hc32_platform PRIVATE
    ${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0/boot/bootutil/include
    ${PROJECT_SOURCE_DIR}/components/mcuboot_port/Inc
)
target_link_libraries(hc32_platform PUBLIC hc32_ll)
