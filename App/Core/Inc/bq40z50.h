#ifndef BQ40Z50_H
#define BQ40Z50_H

#include <stdint.h>

#define BQ40Z50_I2C_ADDRESS 0x0BU

enum bq40z50_result {
    BQ40Z50_OK = 0,
    BQ40Z50_ERR_ARGUMENT = -16,
    BQ40Z50_ERR_PROTOCOL = -17,
};

struct bq40z50_firmware_version {
    uint16_t device_number;
    uint16_t version;
    uint16_t build;
    uint8_t firmware_type;
    uint16_t impedance_track_version;
    uint8_t reserved;
    uint8_t execution_context;
};

struct bq40z50_identity {
    uint16_t device_type;
    struct bq40z50_firmware_version firmware;
    uint16_t hardware_version;
    uint16_t chemistry_id;
};

int bq40z50_read_identity(struct bq40z50_identity* identity);

#endif
