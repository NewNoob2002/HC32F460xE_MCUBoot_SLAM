add_library(fw_update_core STATIC
    src/storage.c
    src/boot_control.c
    src/protocol.c
    src/manager.c
)
target_include_directories(fw_update_core PUBLIC include)
target_link_libraries(fw_update_core PRIVATE hc32_project_options)

add_library(fw_update_mcuboot STATIC
    backends/mcuboot/src/storage_mcuboot.c
    backends/mcuboot/src/boot_control_mcuboot.c
)
target_include_directories(fw_update_mcuboot PUBLIC backends/mcuboot/include)
target_link_libraries(fw_update_mcuboot
    PUBLIC fw_update_core
    PRIVATE mcuboot_bootutil hc32_project_options
)
