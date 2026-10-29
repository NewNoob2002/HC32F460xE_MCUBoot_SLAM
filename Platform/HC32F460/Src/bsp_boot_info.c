#include "bsp_boot_info.h"

#include <stddef.h>
#include <string.h>

bool bsp_boot_info_read(struct bsp_boot_info* info) {
    if (info == NULL)
        return false;
    /* Hide the constant from GCC: addresses below 4 KiB otherwise trigger null-page -Warray-bounds diagnostics. */
    uintptr_t address = BSP_BOOT_INFO_ADDRESS;
    __asm__("" : "+r"(address));
    struct bsp_boot_info record;
    memcpy(&record, (const void*)address, sizeof(record));
    if (record.magic != BSP_BOOT_INFO_MAGIC
        || record.check != BSP_BOOT_INFO_CHECK(record.major, record.minor, record.revision, record.build))
        return false;
    *info = record;
    return true;
}
