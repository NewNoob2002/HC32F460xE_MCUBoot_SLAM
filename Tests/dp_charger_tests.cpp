#include <cassert>
#include "DataProc.h"

class FakeSignals final : public hal::ChargerSignals {
  public:
    bool init_ok = true;
    bool read_ok = true;
    hal::ChargerSample signals{false, true};
    unsigned init_count = 0U;
    unsigned reads = 0U;
    bool init() override {
        ++init_count;
        return init_ok;
    }
    bool read(hal::ChargerSample& sample) override {
        ++reads;
        if (!read_ok)
            return false;
        sample = signals;
        return true;
    }
};
class Subscriber final : public DataNode {
  public:
    unsigned count = 0U;
    dp::ChargerState published{};
    int onEvent(EventParam* event) override {
        assert(event->event == EVENT_PUBLISH);
        const int result = dp::Charger::receive(event, published);
        assert(result == RES_OK);
        ++count;
        return result;
    }
};
static void test_sampling() {
    DataBroker broker;
    FakeSignals signals;
    dp::ChargerNode charger(signals);
    Subscriber sub;
    dp::ChargerState pulled{};
    assert(charger.init(broker, 0U));
    assert(!charger.init(broker, 999U));
    assert(signals.init_count == 1U && signals.reads == 1U);
    assert(sub.init(broker, "sub", DataNode::EVENT_PUBLISH) == DataNode::RES_OK);
    assert(dp::Charger::subscribe(sub) == DataNode::RES_OK);
    assert(dp::Charger::pull(sub, pulled) == DataNode::RES_OK);
    assert(pulled.valid && pulled.power_good && !pulled.interrupt_asserted && pulled.change_count == 1U);
    assert(broker.handleTimers(100U) == 100U && sub.count == 0U);
    signals.signals.interrupt_asserted = true;
    (void)broker.handleTimers(200U);
    assert(sub.count == 1U && sub.published.interrupt_asserted && sub.published.change_count == 2U);
    signals.signals.power_good = false;
    (void)broker.handleTimers(201U);
    assert(sub.count == 1U);
    (void)broker.handleTimers(300U);
    assert(sub.count == 2U && !sub.published.power_good);
    signals.read_ok = false;
    signals.signals.power_good = true;
    (void)broker.handleTimers(400U);
    assert(dp::Charger::pull(sub, pulled) == DataNode::RES_OK);
    assert(!pulled.power_good && pulled.change_count == 3U && sub.count == 2U);
    signals.read_ok = true;
    (void)broker.handleTimers(500U);
    assert(sub.count == 3U && sub.published.power_good);
}
static void test_init_failure() {
    DataBroker broker;
    FakeSignals signals;
    signals.init_ok = false;
    dp::ChargerNode charger(signals);
    DataNode sub;
    assert(charger.init(broker, 0U));
    assert(sub.init(broker, "sub") == DataNode::RES_OK);
    assert(dp::Charger::subscribe(sub) == DataNode::RES_OK);
    dp::ChargerState state{};
    assert(dp::Charger::pull(sub, state) == DataNode::RES_OK);
    assert(!state.valid && state.change_count == 0U && signals.reads == 0U);
    assert(!charger.hasTimer() && broker.handleTimers(1000U) == UINT32_MAX);
}
int main() {
    test_sampling();
    test_init_failure();
    return 0;
}
