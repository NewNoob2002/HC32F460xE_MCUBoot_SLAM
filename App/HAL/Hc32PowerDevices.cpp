#include "Hc32PowerDevices.h"

#include "BQ40Z50.h"
#include "ChargerDevices.h"
#include "I2cBus.h"

namespace {
uint8_t batteryAddress(hal::BatterySlot slot) {
    return slot == hal::BatterySlot::Alternate ? hal::BQ40Z50::AlternateAddress : hal::BQ40Z50::DefaultAddress;
}

void copyIdentity(const hal::BqIdentity& source, hal::BatteryIdentity& target) {
    target.device_type = source.device_type;
    target.firmware.device_number = source.firmware.device_number;
    target.firmware.version = source.firmware.version;
    target.firmware.build = source.firmware.build;
    target.firmware.firmware_type = source.firmware.firmware_type;
    target.firmware.impedance_track_version = source.firmware.impedance_track_version;
    target.firmware.reserved = source.firmware.reserved;
    target.firmware.execution_context = source.firmware.execution_context;
    target.hardware_version = source.hardware_version;
    target.chemistry_id = source.chemistry_id;
}

void copyBattery(const hal::BqStatus& source, hal::BatteryTelemetry& target) {
    target.battery_mode = source.battery_mode;
    target.voltage_mv = source.voltage_mv;
    target.current_ma = source.current_ma;
    target.temperature_dk = source.temperature_dk;
    target.relative_soc_percent = source.relative_soc_percent;
    target.remaining_capacity = source.remaining_capacity;
    target.full_charge_capacity = source.full_charge_capacity;
    target.requested_voltage_mv = source.charging_voltage_mv;
    target.requested_current_ma = source.charging_current_ma;
    target.status_flags = source.battery_status;
    target.cycle_count = source.cycle_count;
}

void copyCharger(const hal::Mp2762aStatus& source, hal::ChargerTelemetry& target) {
    target.status_flags = source.status;
    target.fault_flags = source.fault;
    target.battery_voltage_code = source.battery_voltage;
    target.system_voltage_code = source.system_voltage;
    target.charge_current_code = source.charge_current;
    target.input_voltage_code = source.input_voltage;
    target.input_current_code = source.input_current;
}

void copyChargerConfiguration(const hal::Mp2762aConfig& source, hal::ChargerConfiguration& target) {
    target.input_current_limit_code = source.input_limit;
    target.secondary_input_current_limit_code = source.input_limit2;
    target.charge_current_code = source.charge_current;
    target.charge_voltage_code = source.charge_voltage;
    target.control_flags = source.control;
    target.junction_temperature_code = source.junction_temp;
}
} // namespace

namespace hal {
Status Hc32PowerDevices::init() {
    return bus_.init();
}

Status Hc32PowerDevices::probe(BatterySlot slot) {
    BQ40Z50 battery(bus_, batteryAddress(slot));
    return battery.probe();
}

Status Hc32PowerDevices::probeSource() {
    HUSB238 source(bus_);
    return source.probe();
}

Status Hc32PowerDevices::probeCharger() {
    MP2762A charger(bus_);
    return charger.probe();
}

Status Hc32PowerDevices::readIdentity(BatterySlot slot, BatteryIdentity* identity) {
    if (identity == nullptr)
        return InvalidArgument;
    BQ40Z50 battery(bus_, batteryAddress(slot));
    BqIdentity device_identity{};
    const Status result = battery.readIdentity(&device_identity);
    if (result == Ok)
        copyIdentity(device_identity, *identity);
    return result;
}

Status Hc32PowerDevices::readBattery(BatterySlot slot, BatteryTelemetry* telemetry) {
    if (telemetry == nullptr)
        return InvalidArgument;
    BQ40Z50 battery(bus_, batteryAddress(slot));
    BqStatus device_telemetry{};
    const Status result = battery.readStatus(&device_telemetry);
    if (result == Ok)
        copyBattery(device_telemetry, *telemetry);
    return result;
}

Status Hc32PowerDevices::readSource(SourceContract* contract) {
    if (contract == nullptr)
        return InvalidArgument;
    HUSB238 source(bus_);
    Husb238Status device_contract{};
    const Status result = source.readStatus(&device_contract);
    if (result == Ok)
        *contract = {device_contract.pd_status0, device_contract.pd_status1};
    return result;
}

Status Hc32PowerDevices::readCharger(ChargerTelemetry* telemetry) {
    if (telemetry == nullptr)
        return InvalidArgument;
    MP2762A charger(bus_);
    Mp2762aStatus device_telemetry{};
    const Status result = charger.readStatus(&device_telemetry);
    if (result == Ok)
        copyCharger(device_telemetry, *telemetry);
    return result;
}

Status Hc32PowerDevices::readChargerConfiguration(ChargerConfiguration* configuration) {
    if (configuration == nullptr)
        return InvalidArgument;
    MP2762A charger(bus_);
    Mp2762aConfig device_configuration{};
    const Status result = charger.readConfig(&device_configuration);
    if (result == Ok)
        copyChargerConfiguration(device_configuration, *configuration);
    return result;
}

Status Hc32PowerDevices::setInputCurrentLimit(uint16_t milliamps) {
    MP2762A charger(bus_);
    return charger.setInputCurrentLimit(milliamps);
}

Status Hc32PowerDevices::setChargeVoltage(uint16_t millivolts) {
    MP2762A charger(bus_);
    return charger.setChargeVoltage(millivolts);
}

Status Hc32PowerDevices::setChargeCurrent(uint16_t milliamps) {
    MP2762A charger(bus_);
    return charger.setChargeCurrent(milliamps);
}

Status Hc32PowerDevices::setChargerState(bool suspended, bool charge) {
    MP2762A charger(bus_);
    return charger.setSuspended(suspended, charge);
}

uint32_t Hc32PowerDevices::recoveries() {
    return bus_.recoveries();
}
} // namespace hal
