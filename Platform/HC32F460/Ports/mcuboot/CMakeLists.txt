set(MCUBOOT_ROOT ${PROJECT_SOURCE_DIR}/components/mcuboot-2.4.0)

add_library(mcuboot_port_hc32 STATIC
    Src/config_checks.c
    Src/flash_map_backend.c
    Src/keys.c
    ${MCUBOOT_PUBLIC_KEY_SOURCE}
)
target_include_directories(mcuboot_port_hc32 PUBLIC Inc)
target_include_directories(mcuboot_port_hc32 PRIVATE ${MCUBOOT_ROOT}/boot/bootutil/include)
target_link_libraries(mcuboot_port_hc32 PUBLIC hc32_platform)
