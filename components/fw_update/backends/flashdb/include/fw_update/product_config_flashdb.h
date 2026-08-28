#ifndef FW_UPDATE_PRODUCT_CONFIG_FLASHDB_H
#define FW_UPDATE_PRODUCT_CONFIG_FLASHDB_H

#include "fw_update/product_config.h"

enum fw_update_result fw_update_product_config_flashdb_init(struct fw_update_product_config* config,
                                                            const struct fw_update_product_identity* default_identity);

#endif
