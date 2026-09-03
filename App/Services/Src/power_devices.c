#include "power_devices.h"

#include <stddef.h>

#include "bsp_i2c2.h"
#include "core_debug.h"
#include "husb238.h"
#include "mp2762a.h"

void power_devices_init(struct power_devices_status* status) {
    if (status == NULL)
        return;

    *status = (struct power_devices_status){0};
    status->i2c_init_result = bsp_i2c2_init();
    status->bq_default_probe_result = bsp_i2c2_probe(BQ40Z50_I2C_ADDRESS_DEFAULT);
    status->bq_alternate_probe_result = bsp_i2c2_probe(BQ40Z50_I2C_ADDRESS_ALTERNATE);
    status->husb238_probe_result = bsp_i2c2_probe(HUSB238_I2C_ADDRESS);
    status->mp2762a_probe_result = bsp_i2c2_probe(MP2762A_I2C_ADDRESS);
    status->bq_alternate_identity_result =
        status->bq_alternate_probe_result == BSP_I2C2_OK
            ? bq40z50_read_identity(BQ40Z50_I2C_ADDRESS_ALTERNATE, &status->bq_alternate_identity)
            : status->bq_alternate_probe_result;

    CORE_DEBUG_PRINTF("i2c2=%d bq@0b=%d bq@0c=%d husb@08=%d mp@5c=%d bq_identity=%d", status->i2c_init_result,
                      status->bq_default_probe_result, status->bq_alternate_probe_result, status->husb238_probe_result,
                      status->mp2762a_probe_result, status->bq_alternate_identity_result);
}
