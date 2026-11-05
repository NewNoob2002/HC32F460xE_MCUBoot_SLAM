#include "DataProc.h"
#include "app_confirm.h"

namespace dp {
bool SystemNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::System, EVENT_PULL) != RES_OK)
        return false;
    return watchdog_.init(now_ms);
}
void SystemNode::poll(uint32_t now_ms) {
    ++state_.loop_count;
    watchdog_.poll(now_ms);
}
int SystemNode::confirmAfterHost(bool usb_configured, bool auto_confirm) {
    if (!state_.confirm_pending || !usb_configured)
        return 0;
    state_.confirm_pending = false;
    state_.confirm_result = app_confirm_running_image(auto_confirm);
    return state_.confirm_result;
}
int SystemNode::onEvent(EventParam* event) {
    state_.watchdog_ready = watchdog_.isReady();
    state_.watchdog_pulse_active = watchdog_.isPulseActive();
    return ValueNode::onEvent(event);
}
} // namespace dp
