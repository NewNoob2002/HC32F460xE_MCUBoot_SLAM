#include "DataProc.h"
#include "FakeI2cBus.h"

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
    // The absent pack stops at its first word, the present pack reads all 11, the charger reads 7 registers.
    assert(bus.reads == 19U);
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
    // A second pack that is not full keeps the charging pattern.
    power.bq_status_result[1] = hal::BQ40Z50_OK;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_CHARGING);
    power.i2c_init_result = BSP_I2C2_ERR_INIT;
    assert(dp::batteryLedMode(power, charger) == BSP_STATUS_LED_MODE_APP_NO_PACK);
}
int main() {
    batteryLed();
    batteryRefresh();
    check(BSP_I2C2_OK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_ADDR_NACK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_NOT_READY, BSP_I2C2_ERR_INIT);
    return 0;
}
