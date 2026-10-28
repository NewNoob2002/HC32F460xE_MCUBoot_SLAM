#include "DataProc.h"
#include "app_confirm.h"
#include "bsp_external_watchdog.h"

namespace dp {
bool SystemNode::init(DataBroker& broker, uint32_t now_ms) {
    if (DataNode::init(broker, names::System, EVENT_PULL) != RES_OK)
        return false;
    return bsp_external_watchdog_init(now_ms);
}
void SystemNode::poll(uint32_t now_ms) {
    ++state_.loop_count;
    bsp_external_watchdog_poll(now_ms);
}
int SystemNode::confirm(bool auto_confirm) {
    state_.confirm_result = app_confirm_running_image(auto_confirm);
    return state_.confirm_result;
}
int SystemNode::onEvent(EventParam* event) {
    state_.watchdog_ready = bsp_external_watchdog_is_ready();
    state_.watchdog_pulse_active = bsp_external_watchdog_is_pulse_active();
    return ValueNode::onEvent(event);
}
} // namespace dp
