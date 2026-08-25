#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdint.h>

#define BSP_FLASH_SECTOR_SIZE 0x2000UL
#define BSP_FLASH_OK          0

int32_t bsp_flash_write(uint32_t address, const uint8_t* data, uint32_t length);
int32_t bsp_flash_erase_sector(uint32_t sector);
int32_t bsp_flash_read(uint32_t address, uint8_t* data, uint32_t length);
uint16_t bsp_flash_sector_count(uint32_t size);

#endif
