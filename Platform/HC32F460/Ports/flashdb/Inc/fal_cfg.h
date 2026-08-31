#ifndef HC32_FAL_CFG_H
#define HC32_FAL_CFG_H

#include "boot_memory_map.h"

extern const struct fal_flash_dev hc32_product_config_flash;

#define FAL_DEBUG           0
#define FAL_PRINTF(...)     ((void)0)

#define FAL_FLASH_DEV_TABLE {&hc32_product_config_flash}

#define FAL_PART_HAS_TABLE_CFG
#define FAL_PART_TABLE                                                                                                 \
    {                                                                                                                  \
        {FAL_PART_MAGIC_WORD, "product_kv", "hc32_config", 0, RESERVED_SIZE, 0},                                       \
    }

#endif
