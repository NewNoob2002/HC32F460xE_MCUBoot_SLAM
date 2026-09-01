#include "fw_update/product_config.h"

#include <stddef.h>

static int ascii_alphanumeric(char value) {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static int valid_string(const char* value, size_t maximum_length, int allow_dot) {
    size_t index;

    if (value == NULL)
        return 0;
    for (index = 0U; index <= maximum_length; ++index) {
        if (value[index] == '\0')
            return index != 0U;
        if (!ascii_alphanumeric(value[index]) && (allow_dot == 0 || value[index] != '.'))
            return 0;
    }
    return 0;
}

enum fw_update_result fw_update_product_config_get(const struct fw_update_product_config* config,
                                                   struct fw_update_product_config_state* state) {
    if (config == NULL || config->ops == NULL || config->ops->get == NULL || state == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return config->ops->get(config->context, state);
}

enum fw_update_result fw_update_product_config_set(const struct fw_update_product_config* config,
                                                   const struct fw_update_product_identity* identity) {
    if (config == NULL || config->ops == NULL || config->ops->set == NULL || identity == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return config->ops->set(config->context, identity);
}

int fw_update_product_config_valid_device_serial(const char* value) {
    return valid_string(value, FW_UPDATE_DEVICE_SERIAL_MAX_LENGTH, 0);
}

int fw_update_product_config_valid_hardware_version(const char* value) {
    return valid_string(value, FW_UPDATE_HARDWARE_VERSION_MAX_LENGTH, 1);
}
