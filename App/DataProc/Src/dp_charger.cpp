#include "DataProc.h"

namespace dp {
void ChargerNode::sample() {
    hal::ChargerSample signals{};
    if (!signals_.read(signals))
        return;
    if (state_.valid && signals.interrupt_asserted == state_.interrupt_asserted
        && signals.power_good == state_.power_good)
        return;
    state_.valid = true;
    state_.interrupt_asserted = signals.interrupt_asserted;
    state_.power_good = signals.power_good;
    ++state_.change_count;
    publishState();
}
bool ChargerNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::Charger, EVENT_PULL | EVENT_TIMER) != RES_OK)
        return false;
    if (!signals_.init())
        return true;
    sample();
    startTimer(ChargerPeriodMs, now_ms);
    return true;
}
int ChargerNode::onEvent(EventParam* event) {
    if (event && event->event == EVENT_TIMER) {
        sample();
        return RES_OK;
    }
    return ValueNode::onEvent(event);
}
} // namespace dp
