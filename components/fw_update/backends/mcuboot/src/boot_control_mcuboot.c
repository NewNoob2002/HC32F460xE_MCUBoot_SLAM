#include "fw_update/boot_control_mcuboot.h"

#include <stddef.h>
#include <stdint.h>

#include "bootutil/bootutil_public.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "flash_map_backend/flash_map_backend.h"
#include "product_identity.h"

enum { VALIDATION_BUFFER_SIZE = 256 };

static uint16_t read_le16(const uint8_t* value) {
    return (uint16_t)value[0] | (uint16_t)((uint16_t)value[1] << 8U);
}

static uint32_t read_le32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8U) | ((uint32_t)value[2] << 16U) | ((uint32_t)value[3] << 24U);
}

static int candidate_is_valid(void) {
    static uint8_t validation_buffer[VALIDATION_BUFFER_SIZE];
    struct boot_loader_state state;
    struct image_header header;
    struct image_tlv_iter iterator;
    const struct flash_area* area;
    uint8_t compatibility[HC32_COMPATIBILITY_PAYLOAD_SIZE];
    uint32_t offset = 0U;
    uint16_t length = 0U;
    int valid = 0;
    FIH_DECLARE(validation_result, FIH_FAILURE);

    boot_state_init(&state);
    if (boot_open_all_flash_areas(&state) != 0)
        return 0;
    if (boot_read_sectors(&state, NULL) != 0)
        goto out;
    area = BOOT_IMG_AREA(&state, BOOT_SLOT_SECONDARY);
    if (area == NULL)
        goto out;
    if (flash_area_read(area, 0U, &header, sizeof(header)) != 0)
        goto out;
    FIH_CALL(bootutil_img_validate, validation_result, &state, &header, area, validation_buffer,
             sizeof(validation_buffer), NULL, 0, NULL);
    if (FIH_NOT_EQ(validation_result, FIH_SUCCESS))
        goto out;
    if (bootutil_tlv_iter_begin(&iterator, &header, area, HC32_COMPATIBILITY_TLV_TYPE, true) != 0
        || bootutil_tlv_iter_next(&iterator, &offset, &length, NULL) != 0 || length != sizeof(compatibility)
        || flash_area_read(area, offset, compatibility, sizeof(compatibility)) != 0
        || bootutil_tlv_iter_next(&iterator, &offset, &length, NULL) != 1)
        goto out;

    valid = compatibility[0] == HC32_COMPATIBILITY_FORMAT_VERSION && compatibility[1] == 0U
            && read_le16(&compatibility[2]) == HC32_PRODUCT_BOARD_REVISION
            && read_le32(&compatibility[4]) == HC32_PRODUCT_HARDWARE_ID
            && read_le32(&compatibility[8]) == HC32_PRODUCT_BOARD_ID;

out:
    boot_close_all_flash_areas(&state);
    return valid;
}

static enum fw_update_result request_test_upgrade(void* context) {
    (void)context;
    if (candidate_is_valid() == 0)
        return FW_UPDATE_ERR_BOOT_CONTROL;
    return boot_set_pending_multi(0, 0) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_BOOT_CONTROL;
}

static enum fw_update_result confirm_running_image(void* context) {
    (void)context;
    return boot_set_confirmed_multi(0) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_BOOT_CONTROL;
}

static const struct fw_update_boot_control_ops ops = {
    .request_test_upgrade = request_test_upgrade,
    .confirm_running_image = confirm_running_image,
};

void fw_update_boot_control_mcuboot_init(struct fw_update_boot_control* control) {
    if (control == NULL)
        return;
    control->ops = &ops;
    control->context = NULL;
}
