#ifndef APP_HAL_POWERDEVICES_H
#define APP_HAL_POWERDEVICES_H

#include <cstdint>

#include "Status.h"

namespace hal {
enum class BatterySlot : uint8_t { Default, Alternate };

struct BatteryFirmwareVersion {
    uint16_t device_number;
    uint16_t version;
    uint16_t build;
    uint8_t firmware_type;
    uint16_t impedance_track_version;
    uint8_t reserved;
    uint8_t execution_context;
};

struct BatteryIdentity {
    uint16_t device_type;
    BatteryFirmwareVersion firmware;
    uint16_t hardware_version;
    uint16_t chemistry_id;
};

struct BatteryTelemetry {
    uint16_t battery_mode;
    uint16_t voltage_mv;
    int16_t current_ma;
    uint16_t temperature_dk;
    uint16_t relative_soc_percent;
    uint16_t remaining_capacity;
    uint16_t full_charge_capacity;
    uint16_t requested_voltage_mv;
    uint16_t requested_current_ma;
    uint16_t status_flags;
    uint16_t cycle_count;
};

/** Only the debug protocol consumes these raw HUSB238 PD_STATUS0/1 bytes. */
struct SourceDiagnostics {
    uint8_t pd_status0;
    uint8_t pd_status1;
};

/** Source capability in physical units; consumers apply their own budget/headroom policy. */
struct SourceContract {
    uint16_t current_limit_ma;
    SourceDiagnostics diagnostics;
};

/** Raw MP2762A status and ADC register words; the host applies the documented scaling. */
struct ChargerTelemetry {
    uint8_t status_flags;
    uint8_t fault_flags;
    uint16_t battery_voltage_code;
    uint16_t system_voltage_code;
    uint16_t charge_current_code;
    uint16_t input_voltage_code;
    uint16_t input_current_code;
};

/** MP2762A control registers read back after applying a charging decision. */
struct ChargerConfiguration {
    uint8_t input_current_limit_code;
    uint8_t secondary_input_current_limit_code;
    uint8_t charge_current_code;
    uint8_t charge_voltage_code;
    uint8_t control_flags;
    uint16_t junction_temperature_code;
};

/** Main-loop-only power device facade; callers own both the implementation and output buffers. */
class PowerDevices {
  public:
    static constexpr uint16_t MinChargeVoltageMv = 7425U;
    static constexpr uint16_t MinChargeCurrentMa = 180U;

    virtual Status init() = 0;
    virtual Status probe(BatterySlot slot) = 0;
    virtual Status probeSource() = 0;
    virtual Status probeCharger() = 0;
    virtual Status readIdentity(BatterySlot slot, BatteryIdentity* identity) = 0;
    virtual Status readBattery(BatterySlot slot, BatteryTelemetry* telemetry) = 0;
    virtual Status readSource(SourceContract* contract) = 0;
    virtual Status readCharger(ChargerTelemetry* telemetry) = 0;
    virtual Status readChargerConfiguration(ChargerConfiguration* configuration) = 0;
    virtual Status setInputCurrentLimit(uint16_t milliamps) = 0;
    virtual Status setChargeVoltage(uint16_t millivolts) = 0;
    virtual Status setChargeCurrent(uint16_t milliamps) = 0;
    virtual Status setChargerState(bool suspended, bool charge) = 0;
    virtual uint32_t recoveries() = 0;

  protected:
    ~PowerDevices() = default;
};
} // namespace hal

#endif
