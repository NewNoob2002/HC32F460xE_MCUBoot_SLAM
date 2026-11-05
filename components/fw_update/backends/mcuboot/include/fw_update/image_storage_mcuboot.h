#ifndef FW_UPDATE_IMAGE_STORAGE_MCUBOOT_H
#define FW_UPDATE_IMAGE_STORAGE_MCUBOOT_H

#include "fw_update/storage.h"

/** Initializes a storage instance fixed to MCUboot image 0 Secondary Slot. */
void fw_update_image_storage_mcuboot_init(struct fw_update_storage* storage);

#endif
