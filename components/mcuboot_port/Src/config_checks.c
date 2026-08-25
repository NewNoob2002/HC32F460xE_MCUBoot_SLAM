#include "boot_memory_map.h"
#include "mcuboot_config/mcuboot_config.h"

_Static_assert(MCUBOOT_IMAGE_NUMBER == 1, "Only one image is supported");
_Static_assert(MCUBOOT_MAX_IMG_SECTORS == 25, "Slot sector count changed");
_Static_assert(FLASH_WRITE_ALIGN == 4U, "HC32 Flash writes require 4-byte alignment");
_Static_assert(MCUBOOT_BOOT_MAX_ALIGN == 8, "MCUboot scratch swap requires max alignment >= 8");
