#include <cassert>
#include "Diagnostics.h"

// 用类型化 provider 驱动真实 Diagnostics；不替换 BSP 符号。
template <class State> class Provider final : public dp::ValueNode<State> {
  public:
    State& state() {
        return this->state_;
    }
};
int main() {
    DataBroker broker;
    Provider<dp::PowerState> power;
    Provider<dp::ChargerState> charger;
    Provider<dp::UpdateState> update;
    Provider<dp::SystemState> system;
    assert(power.init(broker, dp::names::Power, DataNode::EVENT_PULL) == DataNode::RES_OK);
    assert(charger.init(broker, dp::names::Charger, DataNode::EVENT_PULL) == DataNode::RES_OK);
    assert(update.init(broker, dp::names::Update, DataNode::EVENT_PULL) == DataNode::RES_OK);
    assert(system.init(broker, dp::names::System, DataNode::EVENT_PULL) == DataNode::RES_OK);
    power.state().bq_default_probe_result = -8;
    update.state().usb.manager_state = 1;
    system.state().loop_count = 3U;
    system.state().watchdog_ready = true;
    charger.state() = {true, true, false, 1U};

    app::DiagnosticsSnapshot snapshot{};
    app::Diagnostics diagnostics(snapshot);
    assert(diagnostics.init(broker, 1000U));
    assert(diagnostics.publisherCount() == 4U);
    assert(snapshot.power.bq_default_probe_result == -8);
    assert(snapshot.system.loop_count == 3U);
    assert(snapshot.charger.interrupt_asserted);
    assert(snapshot.update.usb.manager_state == 1);
    // 不同时间的重复初始化不得重置计时起点、快照、关系或 timer。
    assert(!diagnostics.init(broker, 4000U));
    assert(diagnostics.publisherCount() == 4U);
    assert(snapshot.system.loop_count == 3U);
    app::DiagnosticsSnapshot duplicate_snapshot{};
    app::Diagnostics duplicate(duplicate_snapshot);
    assert(!duplicate.init(broker, 4000U));
    assert(!duplicate.isRegistered() && !duplicate.hasTimer());
    assert(broker.handleTimers(5999U) == 1U && snapshot.report_count == 0U);
    system.state().loop_count = 99U;
    system.state().watchdog_pulse_active = true;
    update.state().usb = {2U, -1, 3, 1U};
    power.state().i2c_init_result = -1;
    assert(broker.handleTimers(6000U) == app::DiagnosticsPeriodMs);
    assert(snapshot.report_count == 1U && snapshot.uptime_ms == 5000U);
    assert(snapshot.system.loop_count == 99U && snapshot.system.watchdog_pulse_active);
    assert(snapshot.update.usb.errors == 2U && snapshot.power.i2c_init_result == -1);
    const dp::ChargerState change{true, false, true, 2U};
    assert(charger.publish(&change, sizeof(change)) == DataNode::RES_OK);
    assert(snapshot.charger.power_good && !snapshot.charger.interrupt_asserted);
    assert(snapshot.charger.change_count == 2U);
    uint32_t wrong_size = 0U;
    assert(charger.publish(&wrong_size, sizeof(wrong_size)) == DataNode::RES_SIZE_MISMATCH);
    assert(snapshot.charger.change_count == 2U);
    update.state().usb.configured = 0U;
    assert(update.publish(&update.state(), sizeof(update.state())) == DataNode::RES_OK);
    assert(snapshot.update.usb.configured == 0U);
    // 失去订阅后，周期 pull 失败必须保留最后一次成功值。
    assert(diagnostics.unsubscribe(dp::names::Charger) == DataNode::RES_OK);
    assert(broker.handleTimers(11000U) == app::DiagnosticsPeriodMs);
    assert(snapshot.charger.change_count == 2U && snapshot.report_count == 2U);
    assert(diagnostics.onEvent(nullptr) == DataNode::RES_PARAM_ERROR);
    return 0;
}
