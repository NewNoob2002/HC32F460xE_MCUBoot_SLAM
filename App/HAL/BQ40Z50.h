#pragma once
#include "I2cBus.h"

namespace hal {
enum BqResult {
    BQ40Z50_OK = 0,
    BQ40Z50_ERR_ARGUMENT = -16,
    BQ40Z50_ERR_PROTOCOL = -17,
};

struct BqFirmwareVersion {
    uint16_t device_number;
    uint16_t version;
    uint16_t build;
    uint8_t firmware_type;
    uint16_t impedance_track_version;
    uint8_t reserved;
    uint8_t execution_context;
};

struct BqIdentity {
    uint16_t device_type;
    struct BqFirmwareVersion firmware;
    uint16_t hardware_version;
    uint16_t chemistry_id;
};

class BQ40Z50 {
  public:
    static constexpr uint8_t DefaultAddress = 0x0BU;
    static constexpr uint8_t AlternateAddress = 0x0CU;
    constexpr BQ40Z50(I2cBus& bus, uint8_t address) : bus_(bus), address_(address) {}
    int probe() {
        return bus_.probe(address_);
    }
    int readIdentity(BqIdentity* identity);

  private:
    I2cBus& bus_;
    uint8_t address_;
    int readManufacturerBlock(uint16_t command, uint8_t* payload, uint8_t payload_size);
};
} // namespace hal
