#ifndef POWER_DEVICES_H
#define POWER_DEVICES_H

#include "bq40z50.h"

struct power_devices_status {
    int i2c_init_result;
    int bq_default_probe_result;
    int bq_alternate_probe_result;
    int husb238_probe_result;
    int mp2762a_probe_result;
    int bq_alternate_identity_result;
    struct bq40z50_identity bq_alternate_identity;
};

void power_devices_init(struct power_devices_status* status);

#endif
