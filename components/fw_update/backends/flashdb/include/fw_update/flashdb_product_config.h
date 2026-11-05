#ifndef FW_UPDATE_FLASHDB_PRODUCT_CONFIG_H
#define FW_UPDATE_FLASHDB_PRODUCT_CONFIG_H

#include "fw_update/product_config.h"

enum fw_update_result fw_update_flashdb_product_config_init(struct fw_update_product_config* config,
                                                            const struct fw_update_product_identity* default_identity);

#endif
