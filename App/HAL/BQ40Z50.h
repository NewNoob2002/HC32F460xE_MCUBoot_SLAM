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

/** Smart Battery Data words as reported; capacities are mAh unless battery_mode bit 15 (CAPM) selects 10 mWh. */
struct BqStatus {
    uint16_t battery_mode;
    uint16_t voltage_mv;
    int16_t current_ma;      // positive while charging
    uint16_t temperature_dk; // 0.1 K
    uint16_t relative_soc_percent;
    uint16_t remaining_capacity;
    uint16_t full_charge_capacity;
    uint16_t charging_voltage_mv; // value the pack requests from the charger
    uint16_t charging_current_ma;
    uint16_t battery_status;
    uint16_t cycle_count;
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
    /** Read-only SBS word reads; *status is written only if every read succeeds. */
    int readStatus(BqStatus* status);
    uint8_t address() const {
        return address_;
    }

  private:
    I2cBus& bus_;
    uint8_t address_;
    int readManufacturerBlock(uint16_t command, uint8_t* payload, uint8_t payload_size);
    int readWord(uint8_t command, uint16_t* value);
};
} // namespace hal
