#ifndef BSP_BOOT_INFO_H
#define BSP_BOOT_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Boot identity record at a fixed Boot Flash address (right after the ICG), so the Application can report the
 * installed Boot version without linking against it. Boot images older than this record have code there instead,
 * which the magic and check word reject. */
#define BSP_BOOT_INFO_ADDRESS 0x00000420UL
#define BSP_BOOT_INFO_MAGIC   0x4F464942UL /* "BIFO" */

struct bsp_boot_info {
    uint32_t magic;
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
    uint32_t build;
    uint32_t check; /* ~(magic ^ major | minor << 8 | revision << 16 ^ build) */
};

#define BSP_BOOT_INFO_CHECK(major, minor, revision, build)                                                             \
    (~(BSP_BOOT_INFO_MAGIC ^ ((uint32_t)(major) | ((uint32_t)(minor) << 8U) | ((uint32_t)(revision) << 16U))           \
       ^ (uint32_t)(build)))

/** Copies the Boot record into *info; returns false (leaving *info untouched) when the record is absent or invalid. */
bool bsp_boot_info_read(struct bsp_boot_info* info);

#ifdef __cplusplus
}
#endif

#endif
