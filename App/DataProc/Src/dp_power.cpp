#include "DataProc.h"
#include "core_debug.h"

namespace dp {
bool PowerNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::Power, EVENT_PULL | EVENT_TIMER | EVENT_PUBLISH) != RES_OK)
        return false;
    // Settings come from FlashDB at startup and from host CONFIG_SET later; both arrive as Settings publications.
    if (subscribe(SettingsName) == RES_OK)
        (void)pull(SettingsName, &settings_, sizeof(settings_));
    state_.bus_init_result = devices_.init();
    state_.battery_default_probe_result = devices_.probe(hal::BatterySlot::Default);
    state_.battery_alternate_probe_result = devices_.probe(hal::BatterySlot::Alternate);
    state_.source_probe_result = devices_.probeSource();
    state_.charger_probe_result = devices_.probeCharger();
    state_.battery_alternate_identity_result =
        state_.battery_alternate_probe_result == hal::Ok
            ? devices_.readIdentity(hal::BatterySlot::Alternate, &state_.battery_alternate_identity)
            : state_.battery_alternate_probe_result;
    CORE_DEBUG_PRINTF("bus=%d battery_default=%d battery_alternate=%d source=%d charger=%d identity=%d",
                      state_.bus_init_result, state_.battery_default_probe_result,
                      state_.battery_alternate_probe_result, state_.source_probe_result, state_.charger_probe_result,
                      state_.battery_alternate_identity_result);
    if (state_.bus_init_result == hal::Ok) {
        // The charger powers up with a 1.5 A input limit; cap it to the source before the first tick so a
        // 500 mA USB port is not dragged down while the pack is still being detected.
        state_.source_status_result = devices_.readSource(&state_.source_contract);
        updateInputLimit();
        state_.charger_control_result = devices_.setInputCurrentLimit(state_.input_limit_ma);
        // Packs may be inserted after startup, so both addresses are polled regardless of the init probe.
        startTimer(BatteryPeriodMs, now_ms);
    }
    return true;
}
void PowerNode::refreshDevices() {
    for (unsigned i = 0U; i < BatteryCount; ++i) {
        state_.battery_status_result[i] = devices_.readBattery(BatterySlots[i], &state_.battery_telemetry[i]);
    }
    state_.charger_status_result = devices_.readCharger(&state_.charger_telemetry);
    state_.source_status_result = devices_.readSource(&state_.source_contract);
    updateChargerMode();
    // Read back after control so telemetry shows what the charger actually holds.
    state_.charger_configuration_result = devices_.readChargerConfiguration(&state_.charger_configuration);
    const uint32_t recoveries = devices_.recoveries();
    if (recoveries != state_.bus_recoveries)
        CORE_DEBUG_PRINTF("bus recovered, total=%lu", static_cast<unsigned long>(recoveries));
    state_.bus_recoveries = recoveries;
}
void PowerNode::updateInputLimit() {
    const uint16_t previous = state_.source_limit_ma;
    state_.source_limit_ma = state_.source_status_result == hal::Ok ? state_.source_contract.current_limit_ma : 500U;
    const auto headroom = static_cast<uint16_t>(state_.source_limit_ma * SourceHeadroomPercent / 100U);
    const auto setting = static_cast<uint16_t>(settingValue(settings_, InputCurrentKey));
    state_.input_limit_ma = setting < headroom ? setting : headroom;
    if (state_.source_limit_ma != previous)
        CORE_DEBUG_PRINTF("source result=%d limit=%umA input=%umA", state_.source_status_result,
                          static_cast<unsigned>(state_.source_limit_ma), static_cast<unsigned>(state_.input_limit_ma));
}
void PowerNode::updateChargerMode() {
    // Only the J2 pack is charged; the output-only pack must not keep the charger awake.
    const bool present = state_.battery_status_result[ChargedPackSlot] == hal::Ok;
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
    // Applied every tick: the charger is input powered and returns to its defaults after a replug.
    updateInputLimit();
    if (present) {
        state_.charger_control_result = applyChargeLimits();
    } else {
        // Wake the sleeping pack only when settings permit charging; no pack request is available yet.
        state_.charge_allowed = false;
        state_.charge_target_mv = 0U;
        state_.charge_target_ma = 0U;
        const bool suspend = state_.charger_mode == ChargerMode::Suspended;
        const auto current = static_cast<uint16_t>(settingValue(settings_, ChargeCurrentKey));
        const bool charge =
            settingValue(settings_, ChargeEnableKey) != 0 && current >= hal::PowerDevices::MinChargeCurrentMa;
        // Stop before unrelated writes; a failed input-limit write must never bypass an explicit disable.
        const hal::Status stop_result = suspend || !charge ? devices_.setChargerState(suspend, false) : hal::Ok;
        state_.charger_control_result = suspend ? hal::Ok : devices_.setInputCurrentLimit(state_.input_limit_ma);
        if (stop_result != hal::Ok)
            state_.charger_control_result = stop_result;
        if (state_.charger_control_result == hal::Ok && !suspend && charge) {
            state_.charger_control_result = devices_.setChargeCurrent(current);
            if (state_.charger_control_result == hal::Ok)
                state_.charger_control_result = devices_.setChargerState(false, true);
        }
        if (state_.charger_control_result != hal::Ok)
            (void)devices_.setChargerState(suspend, false);
    }
    if (state_.charger_mode != previous)
        CORE_DEBUG_PRINTF("charger mode %u->%u result=%d", static_cast<unsigned>(previous),
                          static_cast<unsigned>(state_.charger_mode), state_.charger_control_result);
}
hal::Status PowerNode::applyChargeLimits() {
    // Battery status alarms that require charging to stop: overcurrent, overtemperature and termination alarms.
    constexpr uint16_t StopChargeAlarms = 0x8000U | 0x4000U | 0x1000U;
    const auto& pack = state_.battery_telemetry[ChargedPackSlot];
    const auto clamp = [](uint32_t value, int32_t limit) {
        return static_cast<uint16_t>(limit > 0 && value > static_cast<uint32_t>(limit) ? static_cast<uint32_t>(limit)
                                                                                       : value);
    };
    const uint16_t voltage =
        clamp(clamp(pack.requested_voltage_mv, settingValue(settings_, ChargeVoltageKey)), ChargeVoltageCeilingMv);
    const uint16_t current = clamp(pack.requested_current_ma, settingValue(settings_, ChargeCurrentKey));
    const bool allowed =
        settingValue(settings_, ChargeEnableKey) != 0 && current >= hal::PowerDevices::MinChargeCurrentMa
        && voltage >= hal::PowerDevices::MinChargeVoltageMv && (pack.status_flags & StopChargeAlarms) == 0U;
    if (allowed != state_.charge_allowed || voltage != state_.charge_target_mv || current != state_.charge_target_ma)
        CORE_DEBUG_PRINTF("charge allowed=%u target=%umV/%umA request=%umV/%umA status=0x%04x", allowed ? 1U : 0U,
                          static_cast<unsigned>(voltage), static_cast<unsigned>(current),
                          static_cast<unsigned>(pack.requested_voltage_mv),
                          static_cast<unsigned>(pack.requested_current_ma), static_cast<unsigned>(pack.status_flags));
    state_.charge_allowed = allowed;
    state_.charge_target_mv = allowed ? voltage : 0U;
    state_.charge_target_ma = allowed ? current : 0U;
    if (!allowed) {
        const hal::Status stop_result = devices_.setChargerState(false, false);
        const hal::Status input_result = devices_.setInputCurrentLimit(state_.input_limit_ma);
        return stop_result != hal::Ok ? stop_result : input_result;
    }
    hal::Status result = devices_.setInputCurrentLimit(state_.input_limit_ma);
    // Apply both fast-charge and pre-charge limits before charging is (re)enabled.
    if (result == hal::Ok)
        result = devices_.setChargeVoltage(voltage);
    if (result == hal::Ok)
        result = devices_.setChargeCurrent(current);
    if (result == hal::Ok)
        result = devices_.setChargerState(false, true);
    if (result != hal::Ok)
        (void)devices_.setChargerState(false, false);
    return result;
}
BatteryLedMode batteryLedMode(const PowerState& power, const ChargerState& charger) {
    constexpr uint16_t FullyCharged = 0x0020U; // BatteryStatus FC
    constexpr int16_t ChargingThresholdMa = 20;
    constexpr uint16_t LowPercent = 20U;
    constexpr uint16_t CriticalPercent = 10U;
    if (power.bus_init_result != hal::Ok)
        return BatteryLedMode::NoPack;
    // The LEDs describe the J2 pack only; the output-only pack at 0x0C is not shown.
    const hal::Status result = power.battery_status_result[ChargedPackSlot];
    const bool pending = result == hal::NotReady;
    const bool present = result == hal::Ok;
    const auto& pack = power.battery_telemetry[ChargedPackSlot];
    const bool charging = present && pack.current_ma > ChargingThresholdMa;
    const bool full = present && (pack.status_flags & FullyCharged) != 0U;
    const uint16_t soc = present ? pack.relative_soc_percent : 100U;
    // Before the first refresh keep the plain heartbeat instead of flashing "no pack".
    if (!present)
        return pending ? BatteryLedMode::Updater : BatteryLedMode::NoPack;
    // With input power the pack is either charging or full, even while the charger is in pre-charge or termination.
    if (charging || (charger.valid && charger.power_good))
        return full ? BatteryLedMode::Full : BatteryLedMode::Charging;
    if (soc < CriticalPercent)
        return BatteryLedMode::Critical;
    if (soc <= LowPercent)
        return BatteryLedMode::Low;
    return BatteryLedMode::Updater;
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
