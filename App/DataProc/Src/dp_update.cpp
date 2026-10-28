#include "DataProc.h"

namespace dp {
int UpdateNode::init(DataBroker& broker) {
    const int registered = DataNode::init(broker, names::Update, EVENT_PULL);
    if (registered != RES_OK)
        return registered;
    state_.init_result = usb_fw_update_init();
    usb_fw_update_get_status(&state_.usb);
    return state_.init_result;
}
usb_fw_update_action UpdateNode::poll(uint32_t now_ms) {
    const auto action = usb_fw_update_poll(now_ms);
    usb_fw_update_status next{};
    usb_fw_update_get_status(&next);
    // last_result 每包都可能变化，保留为 pull 值，避免每包广播。
    const bool changed = next.configured != state_.usb.configured || next.manager_state != state_.usb.manager_state
                         || next.errors != state_.usb.errors;
    state_.usb = next;
    if (changed)
        publishState();
    return action;
}
} // namespace dp
