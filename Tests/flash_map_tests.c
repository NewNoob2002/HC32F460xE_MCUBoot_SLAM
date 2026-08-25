#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "bsp_flash.h"
#include "flash_map_backend/flash_map_backend.h"

static uint32_t mock_address;
static uint32_t mock_length;
static uint32_t mock_sector;
static uint32_t mock_erase_calls;
static int32_t mock_result;

int32_t bsp_flash_write(uint32_t address, const uint8_t *data, uint32_t length) {
    assert(data != NULL);
    mock_address = address;
    mock_length = length;
    return mock_result;
}

int32_t bsp_flash_erase_sector(uint32_t sector) {
    mock_sector = sector;
    ++mock_erase_calls;
    return mock_result;
}

int32_t bsp_flash_read(uint32_t address, uint8_t *data, uint32_t length) {
    assert(data != NULL);
    mock_address = address;
    mock_length = length;
    return mock_result;
}

uint16_t bsp_flash_sector_count(uint32_t size) {
    return (uint16_t)((size + FLASH_SECTOR_SIZE - 1U) / FLASH_SECTOR_SIZE);
}

static void test_areas_and_slots(void) {
    const struct flash_area *area = NULL;
    uintptr_t base = 1U;

    assert(flash_area_open(FLASH_AREA_IMAGE_PRIMARY(0), &area) == 0);
    assert(area->fa_off == PRIMARY_SLOT_BASE);
    assert(area->fa_size == PRIMARY_SLOT_SIZE);
    assert(flash_area_open(FLASH_AREA_IMAGE_SECONDARY(0), &area) == 0);
    assert(area->fa_off == SECONDARY_SLOT_BASE);
    assert(flash_area_open(FLASH_AREA_IMAGE_SCRATCH, &area) == 0);
    assert(area->fa_off == SCRATCH_BASE);
    assert(flash_area_open(FLASH_AREA_BOOTLOADER, &area) != 0);
    assert(area == NULL);
    assert(flash_area_id_from_image_slot(0) == FLASH_AREA_ID_PRIMARY);
    assert(flash_area_id_from_image_slot(1) == FLASH_AREA_ID_SECONDARY);
    assert(flash_area_id_from_image_slot(2) < 0);
    assert(flash_area_id_to_multi_image_slot(0, FLASH_AREA_ID_PRIMARY) == 0);
    assert(flash_area_id_to_multi_image_slot(0, FLASH_AREA_ID_SECONDARY) == 1);
    assert(flash_area_id_to_multi_image_slot(1, FLASH_AREA_ID_PRIMARY) < 0);
    assert(flash_device_base(FLASH_DEVICE_INTERNAL_FLASH, &base) == 0);
    assert(base == 0U);
}

static void test_io_bounds_and_alignment(void) {
    const struct flash_area *area;
    uint32_t word = 0U;

    assert(flash_area_open(FLASH_AREA_ID_PRIMARY, &area) == 0);
    mock_result = 0;
    assert(flash_area_read(area, 4U, &word, sizeof(word)) == 0);
    assert(mock_address == PRIMARY_SLOT_BASE + 4U);
    assert(mock_length == sizeof(word));
    assert(flash_area_read(area, area->fa_size, &word, sizeof(word)) != 0);
    assert(flash_area_read(area, 0U, &word, 0U) != 0);

    assert(flash_area_write(area, 0U, &word, sizeof(word)) == 0);
    assert(mock_address == PRIMARY_SLOT_BASE);
    assert(flash_area_write(area, 1U, &word, sizeof(word)) != 0);
    assert(flash_area_write(area, 0U, &word, 2U) != 0);
    assert(flash_area_write(area, area->fa_size, &word, sizeof(word)) != 0);
    assert(flash_area_write(area, 0U, NULL, sizeof(word)) != 0);

    mock_erase_calls = 0U;
    assert(flash_area_erase(area, 0U, FLASH_SECTOR_SIZE * 2U) == 0);
    assert(mock_erase_calls == 2U);
    assert(mock_sector == (PRIMARY_SLOT_BASE / FLASH_SECTOR_SIZE) + 1U);
    assert(flash_area_erase(area, 4U, FLASH_SECTOR_SIZE) != 0);
    assert(flash_area_erase(area, 0U, 0U) != 0);
    assert(flash_area_erase(area, area->fa_size, FLASH_SECTOR_SIZE) != 0);

    mock_result = -7;
    assert(flash_area_write(area, 0U, &word, sizeof(word)) == -7);
    mock_erase_calls = 0U;
    assert(flash_area_erase(area, 0U, FLASH_SECTOR_SIZE * 2U) == -7);
    assert(mock_erase_calls == 1U);
}

static void test_geometry(void) {
    const struct flash_area *area;
    struct flash_sector sectors[MCUBOOT_MAX_IMG_SECTORS];
    struct flash_sector sector;
    uint32_t count = MCUBOOT_MAX_IMG_SECTORS;

    assert(flash_area_get_sectors(FLASH_AREA_ID_PRIMARY, &count, sectors) == 0);
    assert(count == 25U);
    assert(sectors[0].fs_off == 0U);
    assert(sectors[24].fs_off == 24U * FLASH_SECTOR_SIZE);
    assert(sectors[24].fs_size == FLASH_SECTOR_SIZE);

    count = 1U;
    assert(flash_area_get_sectors(FLASH_AREA_ID_SCRATCH, &count, sectors) == 0);
    assert(count == 1U);

    count = 1U;
    assert(flash_area_get_sectors(FLASH_AREA_ID_PRIMARY, &count, sectors) != 0);
    assert(count == 25U);

    assert(flash_area_open(FLASH_AREA_ID_PRIMARY, &area) == 0);
    assert(flash_area_get_sector(area, FLASH_SECTOR_SIZE + 4U, &sector) == 0);
    assert(sector.fs_off == FLASH_SECTOR_SIZE);
    assert(sector.fs_size == FLASH_SECTOR_SIZE);
    assert(flash_area_get_sector(area, area->fa_size, &sector) != 0);
    assert(flash_area_align(area) == FLASH_WRITE_ALIGN);
    assert(flash_area_align(NULL) == 0U);
    assert(flash_area_erased_val(area) == 0xFFU);
}

int main(void) {
    test_areas_and_slots();
    test_io_bounds_and_alignment();
    test_geometry();
    return 0;
}
