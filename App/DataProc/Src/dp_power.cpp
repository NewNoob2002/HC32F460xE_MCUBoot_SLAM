#include "ChargerDevices.h"
#include "DataProc.h"
#include "core_debug.h"

namespace dp {
bool PowerNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::Power, EVENT_PULL | EVENT_TIMER) != RES_OK)
        return false;
    state_.i2c_init_result = bus_.init();
    hal::BQ40Z50 default_pack(bus_, hal::BQ40Z50::DefaultAddress);
    hal::BQ40Z50 alternate_pack(bus_, hal::BQ40Z50::AlternateAddress);
    hal::HUSB238 husb(bus_);
    hal::MP2762A mp(bus_);
    state_.bq_default_probe_result = default_pack.probe();
    state_.bq_alternate_probe_result = alternate_pack.probe();
    state_.husb238_probe_result = husb.probe();
    state_.mp2762a_probe_result = mp.probe();
    state_.bq_alternate_identity_result = state_.bq_alternate_probe_result == BSP_I2C2_OK
                                              ? alternate_pack.readIdentity(&state_.bq_alternate_identity)
                                              : state_.bq_alternate_probe_result;
    CORE_DEBUG_PRINTF("i2c2=%d bq@0b=%d bq@0c=%d husb@08=%d mp@5c=%d bq_identity=%d", state_.i2c_init_result,
                      state_.bq_default_probe_result, state_.bq_alternate_probe_result, state_.husb238_probe_result,
                      state_.mp2762a_probe_result, state_.bq_alternate_identity_result);
    // Packs may be inserted after startup, so both addresses are polled regardless of the init probe.
    if (state_.i2c_init_result == BSP_I2C2_OK)
        startTimer(BatteryPeriodMs, now_ms);
    return true;
}
void PowerNode::refreshDevices() {
    const uint8_t addresses[BatteryCount] = {hal::BQ40Z50::DefaultAddress, hal::BQ40Z50::AlternateAddress};
    for (unsigned i = 0U; i < BatteryCount; ++i) {
        hal::BQ40Z50 pack(bus_, addresses[i]);
        state_.bq_status_result[i] = pack.readStatus(&state_.bq_status[i]);
    }
    hal::MP2762A charger(bus_);
    state_.mp2762a_status_result = charger.readStatus(&state_.mp2762a_status);
}
bsp_status_led_mode batteryLedMode(const PowerState& power, const ChargerState& charger) {
    constexpr uint16_t FullyCharged = 0x0020U; // BatteryStatus FC
    constexpr int16_t ChargingThresholdMa = 20;
    constexpr uint16_t LowPercent = 20U;
    constexpr uint16_t CriticalPercent = 10U;
    if (power.i2c_init_result != BSP_I2C2_OK)
        return BSP_STATUS_LED_MODE_APP_NO_PACK;
    bool present = false;
    bool pending = false;
    bool charging = false;
    bool full = true;
    uint16_t soc = 100U;
    for (unsigned i = 0U; i < BatteryCount; ++i) {
        if (power.bq_status_result[i] == BSP_I2C2_ERR_NOT_READY) {
            pending = true;
            continue;
        }
        if (power.bq_status_result[i] != hal::BQ40Z50_OK)
            continue;
        const auto& pack = power.bq_status[i];
        present = true;
        charging = charging || pack.current_ma > ChargingThresholdMa;
        full = full && (pack.battery_status & FullyCharged) != 0U;
        soc = pack.relative_soc_percent < soc ? pack.relative_soc_percent : soc;
    }
    // Before the first refresh keep the plain heartbeat instead of flashing "no pack".
    if (!present)
        return pending ? BSP_STATUS_LED_MODE_APP_UPDATER : BSP_STATUS_LED_MODE_APP_NO_PACK;
    // With input power the pack is either charging or full, even while the charger is in pre-charge or termination.
    if (charging || (charger.valid && charger.power_good))
        return full ? BSP_STATUS_LED_MODE_APP_FULL : BSP_STATUS_LED_MODE_APP_CHARGING;
    if (soc < CriticalPercent)
        return BSP_STATUS_LED_MODE_APP_CRITICAL;
    if (soc <= LowPercent)
        return BSP_STATUS_LED_MODE_APP_LOW;
    return BSP_STATUS_LED_MODE_APP_UPDATER;
}
int PowerNode::onEvent(EventParam* event) {
    if (event && event->event == EVENT_TIMER) {
        refreshDevices();
        return RES_OK;
    }
    return ValueNode::onEvent(event);
}
} // namespace dp
