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
    assert(power.init(broker));
    assert(!power.init(broker));
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
    assert(!power.hasTimer());
}
int main() {
    check(BSP_I2C2_OK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_ADDR_NACK, BSP_I2C2_OK);
    check(BSP_I2C2_ERR_NOT_READY, BSP_I2C2_ERR_INIT);
    return 0;
}
