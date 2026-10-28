#pragma once
#include <cstring>
#include <type_traits>
#include "BQ40Z50.h"
#include "ChargerSignals.h"
#include "DataBroker.h"
#include "usb_fw_update.h"

namespace dp {
namespace names {
inline constexpr char Power[] = "Power";
inline constexpr char Charger[] = "Charger";
inline constexpr char Update[] = "Update";
inline constexpr char System[] = "System";
inline constexpr char Diagnostics[] = "Diagnostics";
} // namespace names
inline constexpr uint32_t ChargerPeriodMs = 100U;

struct PowerState {
    int i2c_init_result = 0;
    int bq_default_probe_result = 0;
    int bq_alternate_probe_result = 0;
    int husb238_probe_result = 0;
    int mp2762a_probe_result = 0;
    int bq_alternate_identity_result = 0;
    hal::BqIdentity bq_alternate_identity{};
};
struct ChargerState {
    bool valid = false;
    bool interrupt_asserted = false;
    bool power_good = false;
    uint32_t change_count = 0U;
};
struct UpdateState {
    int init_result = 0;
    usb_fw_update_status usb{};
};
struct SystemState {
    int confirm_result = 0;
    uint32_t loop_count = 0U;
    bool watchdog_ready = false;
    bool watchdog_pulse_active = false;
};

template <class State> int readPayload(const DataNode::EventParam* event, State& value) {
    static_assert(std::is_trivially_copyable<State>::value, "Payload must be trivially copyable");
    if (!event || !event->data)
        return DataNode::RES_PARAM_ERROR;
    if (event->size != sizeof(State))
        return DataNode::RES_SIZE_MISMATCH;
    std::memcpy(&value, event->data, sizeof(State));
    return DataNode::RES_OK;
}
/** 类型化同步接口；首次值必须 pull，不能保留回调载荷指针。 */
template <const char* Id, class State> struct Reader {
    static int subscribe(DataNode& caller) {
        return caller.subscribe(Id);
    }
    static int pull(DataNode& caller, State& value) {
        return caller.pull(Id, &value, sizeof(State));
    }
    static int receive(const DataNode::EventParam* event, State& value) {
        if (!event || !event->tran || event->event != DataNode::EVENT_PUBLISH
            || std::strcmp(event->tran->getID(), Id) != 0)
            return DataNode::RES_UNSUPPORTED_REQUEST;
        return readPayload(event, value);
    }
};
using Power = Reader<names::Power, PowerState>;
using Charger = Reader<names::Charger, ChargerState>;
using Update = Reader<names::Update, UpdateState>;
using System = Reader<names::System, SystemState>;

template <class State> class ValueNode : public DataNode {
  public:
    int onEvent(EventParam* event) override {
        return replyPull(event, &state_, sizeof(state_));
    }

  protected:
    State state_{};
    void publishState() {
        const State snapshot = state_;
        (void)publish(&snapshot, sizeof(snapshot));
    }
};

class PowerNode final : public ValueNode<PowerState> {
  public:
    explicit constexpr PowerNode(hal::I2cBus& bus) : bus_(bus) {}
    bool init(DataBroker& broker);

  private:
    hal::I2cBus& bus_;
};
class ChargerNode final : public ValueNode<ChargerState> {
  public:
    explicit constexpr ChargerNode(hal::ChargerSignals& signals) : signals_(signals) {}
    bool init(DataBroker& broker, uint32_t now_ms);
    int onEvent(EventParam* event) override;

  private:
    hal::ChargerSignals& signals_;
    void sample();
};
class UpdateNode final : public ValueNode<UpdateState> {
  public:
    int init(DataBroker& broker);
    usb_fw_update_action poll(uint32_t now_ms);
};
class SystemNode final : public ValueNode<SystemState> {
  public:
    bool init(DataBroker& broker, uint32_t now_ms);
    void poll(uint32_t now_ms);
    int confirm(bool auto_confirm);
    int onEvent(EventParam* event) override;
};
} // namespace dp
