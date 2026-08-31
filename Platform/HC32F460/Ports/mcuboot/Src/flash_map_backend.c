#include "flash_map_backend/flash_map_backend.h"

#include <stddef.h>

#include "bsp_flash.h"

static const struct flash_area areas[] = {
    {FLASH_AREA_ID_PRIMARY, FLASH_DEVICE_INTERNAL_FLASH, 0U, PRIMARY_SLOT_BASE, PRIMARY_SLOT_SIZE},
    {FLASH_AREA_ID_SECONDARY, FLASH_DEVICE_INTERNAL_FLASH, 0U, SECONDARY_SLOT_BASE, SECONDARY_SLOT_SIZE},
    {FLASH_AREA_ID_SCRATCH, FLASH_DEVICE_INTERNAL_FLASH, 0U, SCRATCH_BASE, SCRATCH_SIZE},
    {FLASH_AREA_ID_PRODUCT_CONFIG, FLASH_DEVICE_INTERNAL_FLASH, 0U, RESERVED_BASE, RESERVED_SIZE},
};

static const struct flash_area* find_area(uint8_t id) {
    for (uint32_t i = 0U; i < (sizeof(areas) / sizeof(areas[0])); ++i) {
        if (areas[i].fa_id == id)
            return &areas[i];
    }
    return NULL;
}

static int known_area(const struct flash_area* area) {
    return area != NULL && find_area(area->fa_id) == area;
}

static int valid_range(const struct flash_area* area, uint32_t off, uint32_t len) {
    return known_area(area) && len > 0U && off < area->fa_size && len <= area->fa_size - off;
}

int flash_device_base(uint8_t device_id, uintptr_t* base) {
    if (device_id != FLASH_DEVICE_INTERNAL_FLASH || base == NULL)
        return -1;
    *base = 0U;
    return 0;
}

int flash_area_open(uint8_t id, const struct flash_area** area) {
    if (area == NULL)
        return -1;
    *area = find_area(id);
    return *area == NULL ? -1 : 0;
}

void flash_area_close(const struct flash_area* area) {
    (void)area;
}

int flash_area_read(const struct flash_area* area, uint32_t off, void* dst, uint32_t len) {
    if (dst == NULL || !valid_range(area, off, len))
        return -1;
    return bsp_flash_read(area->fa_off + off, dst, len);
}

int flash_area_write(const struct flash_area* area, uint32_t off, const void* src, uint32_t len) {
    if (src == NULL || !valid_range(area, off, len) || (off % FLASH_WRITE_ALIGN) != 0U
        || (len % FLASH_WRITE_ALIGN) != 0U)
        return -1;
    return bsp_flash_write(area->fa_off + off, src, len);
}

int flash_area_erase(const struct flash_area* area, uint32_t off, uint32_t len) {
    if (!valid_range(area, off, len) || (off % FLASH_SECTOR_SIZE) != 0U || (len % FLASH_SECTOR_SIZE) != 0U)
        return -1;

    for (uint32_t erased = 0U; erased < len; erased += FLASH_SECTOR_SIZE) {
        int result = bsp_flash_erase_sector((area->fa_off + off + erased) / FLASH_SECTOR_SIZE);
        if (result != 0)
            return result;
    }
    return 0;
}

uint32_t flash_area_align(const struct flash_area* area) {
    return known_area(area) ? FLASH_WRITE_ALIGN : 0U;
}

uint8_t flash_area_erased_val(const struct flash_area* area) {
    (void)area;
    return 0xFFU;
}

int flash_area_get_sectors(int area_id, uint32_t* count, struct flash_sector* sectors) {
    const struct flash_area* area = area_id < 0 || area_id > UINT8_MAX ? NULL : find_area((uint8_t)area_id);
    if (area == NULL || count == NULL || sectors == NULL)
        return -1;

    const uint32_t sector_count = area->fa_size / FLASH_SECTOR_SIZE;
    const uint32_t capacity = *count;
    *count = sector_count;
    if (capacity < sector_count)
        return -1;

    for (uint32_t i = 0U; i < sector_count; ++i) {
        sectors[i].fs_off = i * FLASH_SECTOR_SIZE;
        sectors[i].fs_size = FLASH_SECTOR_SIZE;
    }
    return 0;
}

int flash_area_get_sector(const struct flash_area* area, uint32_t off, struct flash_sector* sector) {
    if (!known_area(area) || sector == NULL || off >= area->fa_size)
        return -1;
    sector->fs_off = off - (off % FLASH_SECTOR_SIZE);
    sector->fs_size = FLASH_SECTOR_SIZE;
    return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot) {
    if (image_index != 0)
        return -1;
    if (slot == 0)
        return FLASH_AREA_ID_PRIMARY;
    if (slot == 1)
        return FLASH_AREA_ID_SECONDARY;
    return -1;
}

int flash_area_id_from_image_slot(int slot) {
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id) {
    if (image_index != 0)
        return -1;
    if (area_id == FLASH_AREA_ID_PRIMARY)
        return 0;
    if (area_id == FLASH_AREA_ID_SECONDARY)
        return 1;
    return -1;
}
