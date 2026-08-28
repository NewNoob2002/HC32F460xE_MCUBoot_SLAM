#include <fal.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_map_backend/flash_map_backend.h"

static int config_area_open(const struct flash_area** area) {
    return flash_area_open(FLASH_AREA_ID_PRODUCT_CONFIG, area);
}

static int config_flash_init(void) {
    const struct flash_area* area = NULL;
    return config_area_open(&area) == 0 && area->fa_off == RESERVED_BASE && area->fa_size == RESERVED_SIZE ? 0 : -1;
}

static int config_flash_read(long offset, uint8_t* buffer, size_t size) {
    const struct flash_area* area = NULL;
    if (offset < 0L || buffer == NULL || size == 0U || size > INT_MAX || (unsigned long)offset > UINT32_MAX
        || config_area_open(&area) != 0)
        return -1;
    return flash_area_read(area, (uint32_t)offset, buffer, (uint32_t)size) == 0 ? (int)size : -1;
}

static int config_flash_write(long offset, const uint8_t* buffer, size_t size) {
    const struct flash_area* area = NULL;
    if (offset < 0L || buffer == NULL || size == 0U || size > INT_MAX || (unsigned long)offset > UINT32_MAX
        || config_area_open(&area) != 0)
        return -1;
    return flash_area_write(area, (uint32_t)offset, buffer, (uint32_t)size) == 0 ? (int)size : -1;
}

static int config_flash_erase(long offset, size_t size) {
    const struct flash_area* area = NULL;
    if (offset < 0L || size == 0U || size > INT_MAX || (unsigned long)offset > UINT32_MAX
        || config_area_open(&area) != 0)
        return -1;
    return flash_area_erase(area, (uint32_t)offset, (uint32_t)size) == 0 ? (int)size : -1;
}

const struct fal_flash_dev hc32_product_config_flash = {
    .name = "hc32_config",
    .addr = RESERVED_BASE,
    .len = RESERVED_SIZE,
    .blk_size = FLASH_SECTOR_SIZE,
    .ops = {config_flash_init, config_flash_read, config_flash_write, config_flash_erase},
    .write_gran = FLASH_WRITE_ALIGN * 8U,
};
