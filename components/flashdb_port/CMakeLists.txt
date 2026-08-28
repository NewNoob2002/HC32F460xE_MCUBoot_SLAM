set(FLASHDB_ROOT ${PROJECT_SOURCE_DIR}/components/FlashDB-2.2.0)

add_library(hc32_flashdb STATIC
    ${FLASHDB_ROOT}/src/fdb.c
    ${FLASHDB_ROOT}/src/fdb_kvdb.c
    ${FLASHDB_ROOT}/src/fdb_utils.c
    ${FLASHDB_ROOT}/port/fal/src/fal.c
    ${FLASHDB_ROOT}/port/fal/src/fal_flash.c
    ${FLASHDB_ROOT}/port/fal/src/fal_partition.c
    Src/fal_flash_hc32.c
)
target_include_directories(hc32_flashdb PUBLIC
    Inc
    ${FLASHDB_ROOT}/inc
    ${FLASHDB_ROOT}/port/fal/inc
)
target_link_libraries(hc32_flashdb
    PUBLIC mcuboot_port_hc32
    PRIVATE hc32_project_options
)
