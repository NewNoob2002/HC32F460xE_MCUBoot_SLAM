#ifndef FW_UPDATE_ERROR_H
#define FW_UPDATE_ERROR_H

/** Common result codes returned by portable firmware-update contracts. */
enum fw_update_result {
    FW_UPDATE_OK = 0,
    FW_UPDATE_ERR_INVALID_ARGUMENT = -1,
    FW_UPDATE_ERR_BOUNDS = -2,
    FW_UPDATE_ERR_ALIGNMENT = -3,
    FW_UPDATE_ERR_IO = -4,
    FW_UPDATE_ERR_BOOT_CONTROL = -5,
};

#endif
