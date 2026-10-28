#include <cassert>
#include "DataProc.h"
#include "app_confirm.h"
#include "bsp_external_watchdog.h"

namespace {
unsigned watchdog_polls = 0U;
unsigned usb_polls = 0U;
unsigned usb_inits = 0U;
uint32_t last_watchdog_ms = 0U;
uint32_t last_usb_ms = 0U;
bool watchdog_ready = true;
bool pulse = false;
int confirm_result = 0;
bool confirm_enabled = false;
int usb_init_result = 0;
usb_fw_update_status usb{};
usb_fw_update_action action = USB_FW_UPDATE_ACTION_NONE;
class Observer final : public DataNode {
  public:
    unsigned publishes = 0U;
    dp::UpdateState state{};
    int onEvent(EventParam* event) override {
        const int result = dp::Update::receive(event, state);
        if (result == RES_OK)
            ++publishes;
        return result;
    }
};
} // namespace
// 仅共享 C 运行时边界使用链接替身；I2C/GPIO 的测试通过 HAL 假对象注入。
extern "C" {
bool bsp_external_watchdog_init(uint32_t now_ms) {
    last_watchdog_ms = now_ms;
    return watchdog_ready;
}
void bsp_external_watchdog_poll(uint32_t now_ms) {
    ++watchdog_polls;
    last_watchdog_ms = now_ms;
}
bool bsp_external_watchdog_is_ready() {
    return watchdog_ready;
}
bool bsp_external_watchdog_is_pulse_active() {
    return pulse;
}
int app_confirm_running_image(bool enabled) {
    confirm_enabled = enabled;
    return confirm_result;
}
int usb_fw_update_init() {
    ++usb_inits;
    return usb_init_result;
}
usb_fw_update_action usb_fw_update_poll(uint32_t now_ms) {
    ++usb_polls;
    last_usb_ms = now_ms;
    return action;
}
void usb_fw_update_get_status(usb_fw_update_status* status) {
    *status = usb;
}
}
int main() {
    DataBroker broker;
    dp::SystemNode system;
    dp::UpdateNode update;
    Observer observer;
    assert(system.init(broker, 123U) && last_watchdog_ms == 123U);
    assert(update.init(broker) == 0);
    assert(update.init(broker) == DataNode::RES_DUPLICATE && usb_inits == 1U);
    assert(observer.init(broker, "observer", DataNode::EVENT_PUBLISH) == DataNode::RES_OK);
    assert(dp::Update::subscribe(observer) == DataNode::RES_OK);
    assert(dp::System::subscribe(observer) == DataNode::RES_OK);
    assert(system.confirm(true) == 0 && confirm_enabled);
    confirm_result = -3;
    assert(system.confirm(false) == -3 && !confirm_enabled);
    for (uint32_t now = 1000U; now < 1005U; ++now) {
        system.poll(now);
        assert(update.poll(now) == USB_FW_UPDATE_ACTION_NONE);
    }
    assert(watchdog_polls == 5U && usb_polls == 5U);
    assert(last_watchdog_ms == 1004U && last_usb_ms == 1004U);
    assert(!system.hasTimer() && !update.hasTimer() && broker.handleTimers(1005U) == UINT32_MAX);
    dp::SystemState state{};
    pulse = true;
    assert(dp::System::pull(observer, state) == DataNode::RES_OK);
    assert(state.loop_count == 5U && state.confirm_result == -3 && state.watchdog_ready && state.watchdog_pulse_active);
    usb.last_result = -8;
    (void)update.poll(1006U);
    assert(observer.publishes == 0U);
    dp::UpdateState pulled{};
    assert(dp::Update::pull(observer, pulled) == DataNode::RES_OK && pulled.usb.last_result == -8);
    usb.configured = 1U;
    (void)update.poll(1007U);
    assert(observer.publishes == 1U && observer.state.usb.configured == 1U);
    usb.manager_state = 2;
    (void)update.poll(1008U);
    assert(observer.publishes == 2U);
    ++usb.errors;
    (void)update.poll(1009U);
    assert(observer.publishes == 3U && observer.state.usb.errors == 1U);
    action = USB_FW_UPDATE_ACTION_RESET;
    assert(update.poll(1010U) == USB_FW_UPDATE_ACTION_RESET && observer.publishes == 3U);
    {
        DataBroker failure_broker;
        dp::SystemNode bad_watchdog;
        watchdog_ready = false;
        assert(!bad_watchdog.init(failure_broker, 0U));
        dp::UpdateNode bad_usb;
        usb_init_result = -9;
        assert(bad_usb.init(failure_broker) == -9);
    }
    return 0;
}
