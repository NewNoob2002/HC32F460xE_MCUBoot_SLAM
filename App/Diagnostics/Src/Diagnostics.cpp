#include "Diagnostics.h"
#include "core_debug.h"

namespace {
void logCharger(const dp::ChargerState& charger) {
    (void)charger;
    CORE_DEBUG_PRINTF("charger valid=%u int=%u pg=%u changes=%lu", charger.valid ? 1U : 0U,
                      charger.interrupt_asserted ? 1U : 0U, charger.power_good ? 1U : 0U,
                      static_cast<unsigned long>(charger.change_count));
}
} // namespace
namespace app {
void Diagnostics::pullAll() {
    // pull 失败时保留最后成功值；缺失订阅在初始化时记录。
    (void)dp::Power::pull(*this, snapshot_.power);
    (void)dp::System::pull(*this, snapshot_.system);
    (void)dp::Update::pull(*this, snapshot_.update);
    (void)dp::Charger::pull(*this, snapshot_.charger);
}
void Diagnostics::report(uint32_t now_ms) {
    pullAll();
    snapshot_.uptime_ms = now_ms - started_ms_;
    ++snapshot_.report_count;
    CORE_DEBUG_PRINTF("diag uptime=%lu loops=%lu wdog=%u pulse=%u usb_cfg=%u usb_state=%d usb_err=%lu usb_last=%d "
                      "chg_valid=%u chg_int=%u chg_pg=%u",
                      static_cast<unsigned long>(snapshot_.uptime_ms),
                      static_cast<unsigned long>(snapshot_.system.loop_count),
                      snapshot_.system.watchdog_ready ? 1U : 0U, snapshot_.system.watchdog_pulse_active ? 1U : 0U,
                      static_cast<unsigned>(snapshot_.update.usb.configured), snapshot_.update.usb.manager_state,
                      static_cast<unsigned long>(snapshot_.update.usb.errors), snapshot_.update.usb.last_result,
                      snapshot_.charger.valid ? 1U : 0U, snapshot_.charger.interrupt_asserted ? 1U : 0U,
                      snapshot_.charger.power_good ? 1U : 0U);
}
int Diagnostics::onEvent(EventParam* event) {
    if (!event)
        return RES_PARAM_ERROR;
    if (event->event == EVENT_TIMER) {
        uint32_t now_ms = 0U;
        const int result = dp::readPayload(event, now_ms);
        if (result != RES_OK)
            return result;
        report(now_ms);
        return RES_OK;
    }
    if (event->event != EVENT_PUBLISH || !event->tran)
        return RES_UNSUPPORTED_REQUEST;
    const int charger = dp::Charger::receive(event, snapshot_.charger);
    if (charger != RES_UNSUPPORTED_REQUEST) {
        if (charger == RES_OK)
            logCharger(snapshot_.charger);
        return charger;
    }
    const int update = dp::Update::receive(event, snapshot_.update);
    if (update != RES_UNSUPPORTED_REQUEST)
        return update;
    const int power = dp::Power::receive(event, snapshot_.power);
    if (power != RES_UNSUPPORTED_REQUEST)
        return power;
    return dp::System::receive(event, snapshot_.system);
}
bool Diagnostics::init(DataBroker& broker, uint32_t now_ms) {
    // 必须先注册；重复初始化不得覆盖仍在使用的关系、快照和定时器。
    if (DataNode::init(broker, dp::names::Diagnostics, EVENT_PUBLISH | EVENT_TIMER) != RES_OK)
        return false;
    started_ms_ = now_ms;
    snapshot_ = {};
    for (const char* publisher : {dp::names::Power, dp::names::Charger, dp::names::Update, dp::names::System}) {
        const int result = subscribe(publisher);
        if (result != RES_OK)
            CORE_DEBUG_PRINTF("diag subscribe %s failed: %d", publisher, result);
    }
    pullAll();
    CORE_DEBUG_PRINTF("startup usb=%d confirm=%d", snapshot_.update.init_result, snapshot_.system.confirm_result);
    logCharger(snapshot_.charger);
    startTimer(DiagnosticsPeriodMs, now_ms);
    return true;
}
} // namespace app
