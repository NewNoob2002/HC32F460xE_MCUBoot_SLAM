#include "bsp_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include "hc32_ll.h"

#define BSP_FLASH_PROGRAM_ALIGNMENT 4UL
#define BSP_FLASH_SIZE              (EFM_END_ADDR + 1UL)
#define BSP_FLASH_SECTOR_COUNT      (BSP_FLASH_SIZE / EFM_SECTOR_SIZE)

static bool bsp_flash_range_is_valid(uint32_t address, uint32_t length) {
    return (length > 0UL) && (address < BSP_FLASH_SIZE) && (length <= (BSP_FLASH_SIZE - address));
}

static void bsp_flash_begin_operation(void) {
    LL_PERIPH_WE(LL_PERIPH_EFM);
    EFM_FWMC_Cmd(ENABLE);
    EFM_SetBusStatus(EFM_BUS_HOLD);
}

static void bsp_flash_end_operation(void) {
    EFM_SetBusStatus(EFM_BUS_RELEASE);
    EFM_FWMC_Cmd(DISABLE);
    LL_PERIPH_WP(LL_PERIPH_EFM);
}

int32_t bsp_flash_write(uint32_t address, const uint8_t* data, uint32_t length) {
    int32_t result;

    if ((data == NULL) || !bsp_flash_range_is_valid(address, length)
        || ((address % BSP_FLASH_PROGRAM_ALIGNMENT) != 0UL)) {
        return LL_ERR_INVD_PARAM;
    }
    if (SET != EFM_GetStatus(EFM_FLAG_RDY)) {
        return LL_ERR_NOT_RDY;
    }

    bsp_flash_begin_operation();
    result = EFM_Program(address, data, length);
    bsp_flash_end_operation();

    return result;
}

int32_t bsp_flash_erase_sector(uint32_t sector) {
    int32_t result;
    uint32_t address;

    if (sector >= BSP_FLASH_SECTOR_COUNT) {
        return LL_ERR_INVD_PARAM;
    }
    if (SET != EFM_GetStatus(EFM_FLAG_RDY)) {
        return LL_ERR_NOT_RDY;
    }

    address = EFM_SECTOR_ADDR(sector);
    bsp_flash_begin_operation();
    result = EFM_SectorErase(address);
    bsp_flash_end_operation();

    return result;
}

int32_t bsp_flash_read(uint32_t address, uint8_t* data, uint32_t length) {
    if ((data == NULL) || !bsp_flash_range_is_valid(address, length)) {
        return LL_ERR_INVD_PARAM;
    }

    return EFM_ReadByte(address, data, length);
}

uint16_t bsp_flash_sector_count(uint32_t size) {
    uint32_t sector_count = size / EFM_SECTOR_SIZE;

    if ((size % EFM_SECTOR_SIZE) != 0UL) {
        ++sector_count;
    }

    return (uint16_t)sector_count;
}
