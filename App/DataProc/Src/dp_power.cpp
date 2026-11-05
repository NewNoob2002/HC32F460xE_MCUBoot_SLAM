#include "ChargerDevices.h"
#include "DataProc.h"
#include "core_debug.h"

namespace dp {
bool PowerNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::Power, EVENT_PULL | EVENT_TIMER | EVENT_PUBLISH) != RES_OK)
        return false;
    // Settings come from FlashDB at startup and from host CONFIG_SET later; both arrive as Settings publications.
    if (subscribe(SettingsName) == RES_OK)
        (void)pull(SettingsName, &settings_, sizeof(settings_));
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
    if (state_.i2c_init_result == BSP_I2C2_OK) {
        // The MP2762A powers up with its OTP 1.5 A input limit; cap it to the source before the first tick so a
        // 500 mA USB port is not dragged down while the pack is still being detected.
        state_.husb238_status_result = husb.readStatus(&state_.husb238_status);
        updateInputLimit();
        state_.charger_control_result = mp.setInputCurrentLimit(state_.input_limit_ma);
        // Packs may be inserted after startup, so both addresses are polled regardless of the init probe.
        startTimer(BatteryPeriodMs, now_ms);
    }
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
    hal::HUSB238 husb(bus_);
    state_.husb238_status_result = husb.readStatus(&state_.husb238_status);
    updateChargerMode();
    // Read back after control so telemetry shows what the charger actually holds.
    state_.mp2762a_config_result = charger.readConfig(&state_.mp2762a_config);
    const uint32_t recoveries = bus_.recoveries();
    if (recoveries != state_.i2c_recoveries)
        CORE_DEBUG_PRINTF("i2c bus recovered, total=%lu", static_cast<unsigned long>(recoveries));
    state_.i2c_recoveries = recoveries;
}
uint16_t sourceCurrentLimitMa(int husb238_result, const hal::Husb238Status& status) {
    constexpr uint16_t UsbDefaultMa = 500U;
    if (husb238_result != BSP_I2C2_OK)
        return UsbDefaultMa;
    // PD_SRC_CURRENT codes 0..15 (Register Information Rev 1.1, Table 2).
    static constexpr uint16_t PdCurrentMa[16] = {500U,  700U,  1000U, 1250U, 1500U, 1750U, 2000U, 2250U,
                                                 2500U, 2750U, 3000U, 3250U, 3500U, 4000U, 4500U, 5000U};
    const unsigned pd_voltage = status.pd_status0 >> 4U;
    if (pd_voltage >= 1U && pd_voltage <= 6U)
        return PdCurrentMa[status.pd_status0 & 0x0FU];
    // No explicit contract: Type-C 5V_CURRENT (Table 3); "default" is USB 2.0 500 mA.
    static constexpr uint16_t TypeCCurrentMa[4] = {UsbDefaultMa, 1500U, 2400U, 3000U};
    return TypeCCurrentMa[status.pd_status1 & 0x03U];
}
void PowerNode::updateInputLimit() {
    const uint16_t previous = state_.source_limit_ma;
    state_.source_limit_ma = sourceCurrentLimitMa(state_.husb238_status_result, state_.husb238_status);
    const auto headroom = static_cast<uint16_t>(state_.source_limit_ma * SourceHeadroomPercent / 100U);
    const auto setting = static_cast<uint16_t>(settingValue(settings_, InputCurrentKey));
    state_.input_limit_ma = setting < headroom ? setting : headroom;
    if (state_.source_limit_ma != previous)
        CORE_DEBUG_PRINTF("source pd=0x%02x/0x%02x result=%d limit=%umA input=%umA", state_.husb238_status.pd_status0,
                          state_.husb238_status.pd_status1, state_.husb238_status_result,
                          static_cast<unsigned>(state_.source_limit_ma), static_cast<unsigned>(state_.input_limit_ma));
}
void PowerNode::updateChargerMode() {
    // Only the J2 pack is charged; the output-only pack must not keep the charger awake.
    const bool present = state_.bq_status_result[ChargedPackSlot] == hal::BQ40Z50_OK;
    const ChargerMode previous = state_.charger_mode;
    if (present) {
        state_.charger_mode = ChargerMode::Active;
        charger_ticks_ = 0U;
    } else {
        ++charger_ticks_;
        const uint8_t limit = previous == ChargerMode::Active      ? ChargerSuspendAfterTicks
                              : previous == ChargerMode::Suspended ? ChargerWakeIntervalTicks
                                                                   : ChargerWakeWindowTicks;
        if (charger_ticks_ >= limit) {
            state_.charger_mode = previous == ChargerMode::Suspended ? ChargerMode::WakeProbe : ChargerMode::Suspended;
            charger_ticks_ = 0U;
        }
    }
    // Applied every tick: the MP2762A is powered from the input and returns to its defaults after a replug.
    hal::MP2762A charger(bus_);
    updateInputLimit();
    if (present) {
        state_.charger_control_result = applyChargeLimits(charger);
    } else {
        // Wake the sleeping pack only when settings permit charging; no pack request is available yet.
        state_.charge_allowed = false;
        state_.charge_target_mv = 0U;
        state_.charge_target_ma = 0U;
        const bool suspend = state_.charger_mode == ChargerMode::Suspended;
        const auto current = static_cast<uint16_t>(settingValue(settings_, ChargeCurrentKey));
        const bool charge = settingValue(settings_, ChargeEnableKey) != 0 && current >= hal::MP2762A::MinChargeMa;
        // Stop before unrelated writes; a failed input-limit write must never bypass an explicit disable.
        const int stop_result = suspend || !charge ? charger.setSuspended(suspend, false) : BSP_I2C2_OK;
        state_.charger_control_result = suspend ? BSP_I2C2_OK : charger.setInputCurrentLimit(state_.input_limit_ma);
        if (stop_result != BSP_I2C2_OK)
            state_.charger_control_result = stop_result;
        if (state_.charger_control_result == BSP_I2C2_OK && !suspend && charge) {
            state_.charger_control_result = charger.setChargeCurrent(current);
            if (state_.charger_control_result == BSP_I2C2_OK)
                state_.charger_control_result = charger.setSuspended(false, true);
        }
        if (state_.charger_control_result != BSP_I2C2_OK)
            (void)charger.setSuspended(suspend, false);
    }
    if (state_.charger_mode != previous)
        CORE_DEBUG_PRINTF("charger mode %u->%u result=%d", static_cast<unsigned>(previous),
                          static_cast<unsigned>(state_.charger_mode), state_.charger_control_result);
}
int PowerNode::applyChargeLimits(hal::MP2762A& charger) {
    // BQ40Z50 BatteryStatus alarms that require charging to stop: OCA, TCA and OTA.
    constexpr uint16_t StopChargeAlarms = 0x8000U | 0x4000U | 0x1000U;
    const auto& pack = state_.bq_status[ChargedPackSlot];
    const auto clamp = [](uint32_t value, int32_t limit) {
        return static_cast<uint16_t>(limit > 0 && value > static_cast<uint32_t>(limit) ? static_cast<uint32_t>(limit)
                                                                                       : value);
    };
    const uint16_t voltage =
        clamp(clamp(pack.charging_voltage_mv, settingValue(settings_, ChargeVoltageKey)), ChargeVoltageCeilingMv);
    const uint16_t current = clamp(pack.charging_current_ma, settingValue(settings_, ChargeCurrentKey));
    const bool allowed = settingValue(settings_, ChargeEnableKey) != 0 && current >= hal::MP2762A::MinChargeMa
                         && voltage >= hal::MP2762A::MinChargeMv && (pack.battery_status & StopChargeAlarms) == 0U;
    if (allowed != state_.charge_allowed || voltage != state_.charge_target_mv || current != state_.charge_target_ma)
        CORE_DEBUG_PRINTF("charge allowed=%u target=%umV/%umA request=%umV/%umA status=0x%04x", allowed ? 1U : 0U,
                          static_cast<unsigned>(voltage), static_cast<unsigned>(current),
                          static_cast<unsigned>(pack.charging_voltage_mv),
                          static_cast<unsigned>(pack.charging_current_ma), static_cast<unsigned>(pack.battery_status));
    state_.charge_allowed = allowed;
    state_.charge_target_mv = allowed ? voltage : 0U;
    state_.charge_target_ma = allowed ? current : 0U;
    if (!allowed) {
        const int stop_result = charger.setSuspended(false, false);
        const int input_result = charger.setInputCurrentLimit(state_.input_limit_ma);
        return stop_result != BSP_I2C2_OK ? stop_result : input_result;
    }
    int result = charger.setInputCurrentLimit(state_.input_limit_ma);
    // Apply both fast-charge and pre-charge limits before charging is (re)enabled.
    if (result == BSP_I2C2_OK)
        result = charger.setChargeVoltage(voltage);
    if (result == BSP_I2C2_OK)
        result = charger.setChargeCurrent(current);
    if (result == BSP_I2C2_OK)
        result = charger.setSuspended(false, true);
    if (result != BSP_I2C2_OK)
        (void)charger.setSuspended(false, false);
    return result;
}
bsp_status_led_mode batteryLedMode(const PowerState& power, const ChargerState& charger) {
    constexpr uint16_t FullyCharged = 0x0020U; // BatteryStatus FC
    constexpr int16_t ChargingThresholdMa = 20;
    constexpr uint16_t LowPercent = 20U;
    constexpr uint16_t CriticalPercent = 10U;
    if (power.i2c_init_result != BSP_I2C2_OK)
        return BSP_STATUS_LED_MODE_APP_NO_PACK;
    // The LEDs describe the J2 pack only; the output-only pack at 0x0C is not shown.
    const int result = power.bq_status_result[ChargedPackSlot];
    const bool pending = result == BSP_I2C2_ERR_NOT_READY;
    const bool present = result == hal::BQ40Z50_OK;
    const auto& pack = power.bq_status[ChargedPackSlot];
    const bool charging = present && pack.current_ma > ChargingThresholdMa;
    const bool full = present && (pack.battery_status & FullyCharged) != 0U;
    const uint16_t soc = present ? pack.relative_soc_percent : 100U;
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
    // New settings take effect on the next 1 s tick, which also re-applies them after a charger reset.
    if (event && event->event == EVENT_PUBLISH && event->tran && std::strcmp(event->tran->getID(), SettingsName) == 0)
        return readPayload(event, settings_);
    return ValueNode::onEvent(event);
}
} // namespace dp
