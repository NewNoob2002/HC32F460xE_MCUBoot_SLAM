#include "DataProc.h"
#include "FakeI2cBus.h"

/** In-memory settings store; nothing is preloaded, so the table defaults apply. */
class Store final : public hal::SettingsStore {
  public:
    bool load(uint8_t*, std::size_t, std::size_t& length) override {
        length = 0U;
        return false;
    }
    bool save(const uint8_t*, std::size_t) override {
        return true;
    }
};

static void check(int alternate_result, int init_result) {
    DataBroker broker;
    FakeI2cBus bus;
    bus.probe_results[0] = BSP_I2C2_ERR_ADDR_NACK;
    bus.probe_results[1] = alternate_result;
    bus.probe_results[2] = BSP_I2C2_ERR_BUS_BUSY;
    bus.init_result = init_result;
    dp::PowerNode power(bus);
    DataNode sub;
    assert(power.init(broker, 0U));
    assert(!power.init(broker, 0U));
    assert(bus.init_count == 1U && bus.probe_count == 4U);
    assert((bus.probes == std::array<uint8_t, 4>{0x0BU, 0x0CU, 0x08U, 0x5CU}));
    assert(sub.init(broker, "sub") == DataNode::RES_OK);
    assert(dp::Power::subscribe(sub) == DataNode::RES_OK);
    dp::PowerState state{};
    assert(dp::Power::pull(sub, state) == DataNode::RES_OK);
    assert(state.i2c_init_result == init_result);
    assert(state.bq_default_probe_result == BSP_I2C2_ERR_ADDR_NACK);
    assert(state.bq_alternate_probe_result == alternate_result);
    assert(state.husb238_probe_result == BSP_I2C2_ERR_BUS_BUSY);
    assert(state.mp2762a_probe_result == BSP_I2C2_OK);
    if (alternate_result == BSP_I2C2_OK) {
        assert(bus.reads == 4U && state.bq_alternate_identity.device_type == 0x4500U);
        assert(state.bq_alternate_identity_result == hal::BQ40Z50_OK);
    } else {
        assert(bus.reads == 0U && bus.writes == 0U);
        assert(state.bq_alternate_identity_result == alternate_result);
    }
    assert(sub.notify(dp::names::Power, &state, sizeof(state)) == DataNode::RES_UNSUPPORTED_REQUEST);
    // Battery polling starts only on a working bus; until the first tick both slots stay invalid.
    assert(power.hasTimer() == (init_result == BSP_I2C2_OK));
    assert(state.bq_status_result[0] == BSP_I2C2_ERR_NOT_READY);
    assert(state.bq_status_result[1] == BSP_I2C2_ERR_NOT_READY);
}

/* Each timer tick reads both pack addresses and keeps a per-slot result. */
static void batteryRefresh() {
    DataBroker broker;
    FakeI2cBus bus;
    bus.probe_results[1] = BSP_I2C2_ERR_ADDR_NACK; // skip the identity read
    dp::PowerNode power(bus);
    DataNode sub;
    assert(power.init(broker, 0U));
    assert(sub.init(broker, "sub") == DataNode::RES_OK && dp::Power::subscribe(sub) == DataNode::RES_OK);
    bus.sbs = true;
    bus.read_result = BSP_I2C2_ERR_ADDR_NACK;
    bus.sbs_absent_address = hal::BQ40Z50::DefaultAddress;
    bus.sbs_words[0x09] = 7900U;
    bus.sbs_words[0x0D] = 55U;
    bus.mp_regs[0x13] = 0x08U;
    bus.mp_regs[0x14] = 0x40U;
    bus.mp_regs[0x16] = 0x34U; // battery voltage word 0x1234, little-endian
    bus.mp_regs[0x17] = 0x12U;
    bus.mp_regs[0x1F] = 0xABU;
    (void)broker.handleTimers(dp::BatteryPeriodMs - 1U);
    assert(bus.reads == 0U);
    (void)broker.handleTimers(dp::BatteryPeriodMs);
    // The absent pack stops at its first word, the present pack reads all 11, the charger reads 7 status registers
    // plus REG08H before and after enabling charging for the present pack.
    assert(bus.reads == 21U && bus.mp_writes == 1U && bus.mp_regs[0x08] == 0x10U);
    dp::PowerState state{};
    assert(dp::Power::pull(sub, state) == DataNode::RES_OK);
    assert(state.bq_status_result[0] == BSP_I2C2_ERR_ADDR_NACK);
    assert(state.bq_status_result[1] == hal::BQ40Z50_OK);
    assert(state.bq_status[1].voltage_mv == 7900U && state.bq_status[1].relative_soc_percent == 55U);
    assert(state.mp2762a_status_result == BSP_I2C2_OK);
    assert(state.mp2762a_status.status == 0x08U && state.mp2762a_status.fault == 0x40U);
    assert(state.mp2762a_status.battery_voltage == 0x1234U && state.mp2762a_status.input_current == 0xAB00U);

    // A failed charger read keeps the previous registers and reports the error.
    bus.mp_result = BSP_I2C2_ERR_ADDR_NACK;
    bus.mp_regs[0x13] = 0U;
    (void)broker.handleTimers(2U * dp::BatteryPeriodMs);
    assert(dp::Power::pull(sub, state) == DataNode::RES_OK);
    assert(state.mp2762a_status_result == BSP_I2C2_ERR_ADDR_NACK && state.mp2762a_status.status == 0x08U);
    assert(state.charger_mode == dp::ChargerMode::Active && state.charger_control_result == BSP_I2C2_ERR_ADDR_NACK);
}
/* No pack: suspend the charger after a debounce, keep re-applying it, and lift it periodically for a sleeping pack. */
static void chargerSuspend() {
    DataBroker broker;
    FakeI2cBus bus;
    bus.probe_results[1] = BSP_I2C2_ERR_ADDR_NACK;
    dp::PowerNode power(bus);
    DataNode sub;
    assert(power.init(broker, 0U));
    assert(sub.init(broker, "sub") == DataNode::RES_OK && dp::Power::subscribe(sub) == DataNode::RES_OK);
    bus.sbs = true;
    bus.read_result = BSP_I2C2_ERR_ADDR_NACK;
    bus.sbs_absent_address = hal::BQ40Z50::DefaultAddress;
    bus.sbs_fail_reg = 0x03;   // the alternate pack fails its first word: both packs absent
    bus.mp_regs[0x08] = 0x96U; // V1.1 read-back 0x16 plus a set REG_RST bit, which must never be written back
    uint32_t now = 0U;
    dp::PowerState state{};
    const auto tick = [&]() {
        now += dp::BatteryPeriodMs;
        (void)broker.handleTimers(now);
        assert(dp::Power::pull(sub, state) == DataNode::RES_OK);
    };
    for (uint8_t i = 1U; i < dp::ChargerSuspendAfterTicks; ++i) {
        tick();
        assert(state.charger_mode == dp::ChargerMode::Active);
    }
    assert(bus.mp_writes == 1U && bus.mp_regs[0x08] == 0x16U); // only REG_RST cleared, CHG_EN kept on
    tick();
    // Suspend sets SUSP_EN and clears CHG_EN and nothing else.
    assert(state.charger_mode == dp::ChargerMode::Suspended && state.charger_control_result == BSP_I2C2_OK);
    assert(bus.mp_writes == 2U && bus.mp_regs[0x08] == 0x0EU);
    tick();
    assert(bus.mp_writes == 2U); // already suspended: no write
    bus.mp_regs[0x08] = 0x16U;   // input replug resets the MP2762A to its defaults
    tick();
    assert(bus.mp_writes == 3U && bus.mp_regs[0x08] == 0x0EU);
    while (state.charger_mode == dp::ChargerMode::Suspended)
        tick();
    // The wake window starts ChargerWakeIntervalTicks after entering suspend (3 ticks already elapsed above).
    assert(now == (dp::ChargerSuspendAfterTicks + dp::ChargerWakeIntervalTicks) * dp::BatteryPeriodMs);
    assert(state.charger_mode == dp::ChargerMode::WakeProbe && bus.mp_regs[0x08] == 0x16U);
    for (uint8_t i = 1U; i < dp::ChargerWakeWindowTicks; ++i) {
        tick();
        assert(state.charger_mode == dp::ChargerMode::WakeProbe);
    }
    tick();
    assert(state.charger_mode == dp::ChargerMode::Suspended && bus.mp_regs[0x08] == 0x0EU);
    // The output-only pack (0x0C) answering must not wake the charger.
    bus.sbs_fail_reg = -1;
    tick();
    assert(state.bq_status_result[1] == hal::BQ40Z50_OK && state.charger_mode == dp::ChargerMode::Suspended);
    // The charged pack (0x0B) answering resumes charging at once, with its request applied.
    bus.expected_address = hal::BQ40Z50::DefaultAddress;
    bus.sbs_absent_address = hal::BQ40Z50::AlternateAddress;
    bus.sbs_words[0x15] = 8200U;
    bus.sbs_words[0x14] = 2000U;
    tick();
    assert(state.charger_mode == dp::ChargerMode::Active && state.charge_allowed);
    assert(bus.mp_regs[0x08] == 0x16U && state.charger_control_result == BSP_I2C2_OK);
    // A register that does not take the value is reported, not assumed.
    bus.sbs_fail_reg = 0x03;
    bus.mp_stuck_bits = 0x10U;
    for (uint8_t i = 0U; i < dp::ChargerSuspendAfterTicks; ++i)
        tick();
    assert(state.charger_mode == dp::ChargerMode::Suspended && state.charger_control_result == hal::Mp2762aErrVerify);
}
/* The charged pack's request, capped by the settings and the 8.4 V ceiling, is written before charging is enabled. */
static void chargeLimits() {
    DataBroker broker;
    Store store;
    dp::SettingsNode settings(store);
    FakeI2cBus bus;
    bus.probe_results[1] = BSP_I2C2_ERR_ADDR_NACK;
    assert(settings.init(broker));
    dp::PowerNode power(bus);
    DataNode sub;
    assert(power.init(broker, 0U));
    assert(sub.init(broker, "sub") == DataNode::RES_OK && dp::Power::subscribe(sub) == DataNode::RES_OK);
    bus.sbs = true;
    bus.read_result = BSP_I2C2_ERR_ADDR_NACK;
    bus.expected_address = hal::BQ40Z50::DefaultAddress;
    bus.sbs_absent_address = hal::BQ40Z50::AlternateAddress;
    bus.sbs_words[0x15] = 8200U; // ChargingVoltage
    bus.sbs_words[0x14] = 2000U; // ChargingCurrent
    bus.mp_regs[0x00] = 0x1EU;   // V1.1 OTP defaults: 1.5 A input, 1 A charge, 8.4 V with VRECH_OS kept
    bus.mp_regs[0x02] = 0x14U;
    bus.mp_regs[0x04] = 0x4FU;
    bus.mp_regs[0x08] = 0x16U;
    uint32_t now = 0U;
    dp::PowerState state{};
    const auto tick = [&]() {
        now += dp::BatteryPeriodMs;
        (void)broker.handleTimers(now);
        assert(dp::Power::pull(sub, state) == DataNode::RES_OK);
    };
    tick();
    // Defaults allow the pack's own 8.2 V / 2.0 A request.
    assert(state.charge_allowed && state.charge_target_mv == 8200U && state.charge_target_ma == 2000U);
    assert(bus.mp_regs[0x04] == ((8200U - 7425U) / 25U << 1U | 1U) && bus.mp_regs[0x02] == 40U);
    assert(bus.mp_regs[0x00] == 30U && bus.mp_regs[0x08] == 0x16U);
    // Settings cap the request; the 8.4 V ceiling caps a pack that asks for more.
    assert(settings.set(dp::ChargeCurrentKey, 1210) == dp::SettingResult::Ok);
    assert(settings.set(dp::InputCurrentKey, 1000) == dp::SettingResult::Ok);
    bus.sbs_words[0x15] = 8700U;
    tick();
    assert(state.charge_target_mv == 8400U && state.charge_target_ma == 1210U);
    assert(bus.mp_regs[0x04] == ((8400U - 7425U) / 25U << 1U | 1U) && bus.mp_regs[0x02] == 24U);
    assert(bus.mp_regs[0x00] == 20U);
    // A replug restores the OTP defaults; the next tick writes the limits again.
    bus.mp_regs[0x02] = 0x14U;
    tick();
    assert(bus.mp_regs[0x02] == 24U);
    // charge_enable=0, a zero request or a stop-charge alarm turn CHG_EN off but keep the DC/DC running.
    assert(settings.set(dp::ChargeEnableKey, 0) == dp::SettingResult::Ok);
    tick();
    assert(!state.charge_allowed && state.charge_target_ma == 0U && bus.mp_regs[0x08] == 0x06U);
    assert(settings.set(dp::ChargeEnableKey, 1) == dp::SettingResult::Ok);
    tick();
    assert(state.charge_allowed && bus.mp_regs[0x08] == 0x16U);
    bus.sbs_words[0x14] = 0U;
    tick();
    assert(!state.charge_allowed && bus.mp_regs[0x08] == 0x06U);
    bus.sbs_words[0x14] = 2000U;
    bus.sbs_words[0x16] = 0x4000U; // TCA
    tick();
    assert(!state.charge_allowed && bus.mp_regs[0x08] == 0x06U);
    bus.sbs_words[0x16] = 0U;
    // A limit that does not take leaves charging off.
    bus.mp_stuck_bits = 0x01U;
    bus.mp_regs[0x02] = 0U;
    tick();
    assert(state.charger_control_result == hal::Mp2762aErrVerify && (bus.mp_regs[0x08] & 0x10U) == 0U);
}
/* LED pattern priorities: unknown -> heartbeat, input power -> charging/full, then the lowest pack SOC. */
static void batteryLed() {
    dp::PowerState power{};
    dp::ChargerState charger{};
    power.i2c_init_result = BSP_I2C2_OK;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_UPDATER);
    power.bq_status_result[0] = BSP_I2C2_ERR_ADDR_NACK;
    power.bq_status_result[1] = BSP_I2C2_ERR_ADDR_NACK;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_NO_PACK);
    power.bq_status_result[0] = hal::BQ40Z50_OK;
    power.bq_status[0].relative_soc_percent = 21U;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_UPDATER);
    power.bq_status[0].relative_soc_percent = 20U;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_LOW);
    power.bq_status[0].relative_soc_percent = 9U;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_CRITICAL);
    power.bq_status[0].current_ma = 21;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_CHARGING);
    power.bq_status[0].current_ma = 0;
    charger.valid = true;
    charger.power_good = true;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_CHARGING);
    power.bq_status[0].battery_status = 0x0020U;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_FULL);
    // The output-only pack is never shown: a low or absent 0x0C pack does not change the J2 pattern.
    power.bq_status_result[1] = hal::BQ40Z50_OK;
    power.bq_status[1].relative_soc_percent = 5U;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_FULL);
    power.bq_status_result[0] = BSP_I2C2_ERR_ADDR_NACK;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_NO_PACK);
    power.bq_status_result[0] = hal::BQ40Z50_OK;
    power.i2c_init_result = BSP_I2C2_ERR_INIT;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_NO_PACK);
}
int main() {
    batteryLed();
    batteryRefresh();
    chargerSuspend();
    chargeLimits();
    check(BSP_I2C2_OK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_ADDR_NACK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_NOT_READY, BSP_I2C2_ERR_INIT);
    return 0;
}
