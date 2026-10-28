#include "ChargerDevices.h"
#include "DataProc.h"
#include "core_debug.h"

namespace dp {
bool PowerNode::init(DataBroker& broker) {
    if (DataNode::init(broker, names::Power, EVENT_PULL) != RES_OK)
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
    return true;
}
} // namespace dp
