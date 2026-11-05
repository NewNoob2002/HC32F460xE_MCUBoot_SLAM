add_library(fw_update_core STATIC
    src/storage.c
    src/admin_auth.c
    src/boot_control.c
    src/product_config.c
    src/protocol.c
    src/manager.c
)
hc32_apply_project_options(fw_update_core)
target_include_directories(fw_update_core PUBLIC include)
target_link_libraries(fw_update_core PRIVATE hc32_project_options)

add_library(fw_update_mcuboot STATIC
    backends/flashdb/src/flashdb_product_config.c
    backends/mcuboot/src/image_storage_mcuboot.c
    backends/mcuboot/src/image_control_mcuboot.c
    backends/mcuboot/src/admin_ecdsa_auth.c
)
hc32_apply_project_options(fw_update_mcuboot)
target_include_directories(fw_update_mcuboot PUBLIC
    backends/flashdb/include
    backends/mcuboot/include
)
target_include_directories(fw_update_mcuboot SYSTEM PRIVATE ${BOOTUTIL_ROOT}/src)
target_link_libraries(fw_update_mcuboot
    PUBLIC fw_update_core
    PRIVATE flashdb_port_hc32 mcuboot_bootutil tinycrypt hc32_project_options
)
