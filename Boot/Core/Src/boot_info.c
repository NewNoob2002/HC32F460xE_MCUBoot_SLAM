#include "bsp_boot_info.h"

/* Placed at BSP_BOOT_INFO_ADDRESS by Linker/boot.ld; BOOT_VERSION_* come from the BOOT_VERSION CMake cache value. */
__attribute__((section(".boot_info"), used)) const struct bsp_boot_info g_boot_info = {
    .magic = BSP_BOOT_INFO_MAGIC,
    .major = BOOT_VERSION_MAJOR,
    .minor = BOOT_VERSION_MINOR,
    .revision = BOOT_VERSION_REVISION,
    .build = BOOT_VERSION_BUILD,
    .check = BSP_BOOT_INFO_CHECK(BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR, BOOT_VERSION_REVISION, BOOT_VERSION_BUILD),
};
