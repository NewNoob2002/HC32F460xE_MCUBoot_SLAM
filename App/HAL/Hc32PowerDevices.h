#ifndef APP_HAL_HC32POWERDEVICES_H
#define APP_HAL_HC32POWERDEVICES_H

#include "PowerDevices.h"

namespace hal {
class I2cBus;

class Hc32PowerDevices final : public PowerDevices {
  public:
    explicit constexpr Hc32PowerDevices(I2cBus& bus) : bus_(bus) {}

    Status init() override;
    Status probe(BatterySlot slot) override;
    Status probeSource() override;
    Status probeCharger() override;
    Status readIdentity(BatterySlot slot, BatteryIdentity* identity) override;
    Status readBattery(BatterySlot slot, BatteryTelemetry* telemetry) override;
    Status readSource(SourceContract* contract) override;
    Status readCharger(ChargerTelemetry* telemetry) override;
    Status readChargerConfiguration(ChargerConfiguration* configuration) override;
    Status setInputCurrentLimit(uint16_t milliamps) override;
    Status setChargeVoltage(uint16_t millivolts) override;
    Status setChargeCurrent(uint16_t milliamps) override;
    Status setChargerState(bool suspended, bool charge) override;
    uint32_t recoveries() override;

  private:
    I2cBus& bus_;
};
} // namespace hal

#endif
