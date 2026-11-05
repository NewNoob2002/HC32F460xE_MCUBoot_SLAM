#ifndef APP_DATAPROC_DATAPROC_H
#define APP_DATAPROC_DATAPROC_H
#include <array>
#include <cstring>
#include <type_traits>
#include "ChargerSignals.h"
#include "DataBroker.h"
#include "PowerDevices.h"
#include "Settings.h"
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
inline constexpr uint32_t BatteryPeriodMs = 1000U;
/** Battery slot 0 is the default pack, slot 1 the alternate pack. */
inline constexpr unsigned BatteryCount = 2U;
inline constexpr std::array<hal::BatterySlot, BatteryCount> BatterySlots{hal::BatterySlot::Default,
                                                                         hal::BatterySlot::Alternate};
/** Without a pack the charger DC/DC only cycles into battery OVP, so it is suspended after this many absent ticks. */
inline constexpr uint8_t ChargerSuspendAfterTicks = 3U;
/** A pack in shutdown answers only after the charger drives its power path, so suspend is lifted periodically.
 * Window length is a first guess that HIL must confirm against a sleeping pack. */
inline constexpr uint8_t ChargerWakeIntervalTicks = 60U;
inline constexpr uint8_t ChargerWakeWindowTicks = 5U;
enum class ChargerMode : uint8_t { Active, Suspended, WakeProbe };
/** The default slot is charged through J2; the alternate pack is output-only and never charged. */
inline constexpr unsigned ChargedPackSlot = static_cast<unsigned>(hal::BatterySlot::Default);
/** Hard 2S charge-voltage ceiling, independent of settings and pack requests. */
inline constexpr uint16_t ChargeVoltageCeilingMv = 8400U;
/** Input current the source allows, from an explicit PD contract or the Type-C 5 V advertisement. An unreadable
 * source or a default-current source falls back to USB 2.0's 500 mA. */
uint16_t sourceCurrentLimitMa(hal::Status source_result, const hal::SourceContract& contract);
/** The charger input limit leaves headroom for the main board and source on the same input and for the charger's
 * current-limit tolerance. */
inline constexpr unsigned SourceHeadroomPercent = 90U;

struct PowerState {
    hal::Status bus_init_result = hal::Ok;
    hal::Status battery_default_probe_result = hal::Ok;
    hal::Status battery_alternate_probe_result = hal::Ok;
    hal::Status source_probe_result = hal::Ok;
    hal::Status charger_probe_result = hal::Ok;
    hal::Status battery_alternate_identity_result = hal::Ok;
    hal::BatteryIdentity battery_alternate_identity{};
    // Not read until the first BatteryPeriodMs tick; nonzero result keeps the previous status invalid.
    hal::Status battery_status_result[BatteryCount] = {hal::NotReady, hal::NotReady};
    hal::BatteryTelemetry battery_telemetry[BatteryCount]{};
    hal::Status charger_status_result = hal::NotReady;
    hal::ChargerTelemetry charger_telemetry{};
    ChargerMode charger_mode = ChargerMode::Active;
    hal::Status charger_control_result = hal::NotReady;
    // Last limits computed from the charged pack's request and the settings; zero while charging is not allowed.
    bool charge_allowed = false;
    uint16_t charge_target_mv = 0U;
    uint16_t charge_target_ma = 0U;
    hal::Status source_status_result = hal::NotReady;
    hal::SourceContract source_contract{};
    uint16_t source_limit_ma = 0U; // what the source allows
    uint16_t input_limit_ma = 0U;  // what was written to REG00H
    hal::Status charger_configuration_result = hal::NotReady;
    hal::ChargerConfiguration charger_configuration{};
    uint32_t bus_recoveries = 0U;
};
struct ChargerState {
    bool valid = false;
    bool interrupt_asserted = false;
    bool power_good = false;
    uint32_t change_count = 0U;
    SettingsState settings{};
};
struct UpdateState {
    int init_result = 0;
    usb_fw_update_status usb{};
};
struct SystemState {
    int confirm_result = 0;
    // The running image is confirmed only after a host enumerated USB; until then a reset lets MCUboot revert.
    bool confirm_pending = true;
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
/** Typed synchronous interface; pull the initial value and never retain callback payload pointers. */
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

    const State& state() const {
        return state_;
    }

  protected:
    State state_{};
    void publishState() {
        const State snapshot = state_;
        (void)publish(&snapshot, sizeof(snapshot));
    }
};

/** App idle LED pattern from the last battery refresh and the charger PG pin (DEBUG_LINK_V1.md has the units). */
bsp_status_led_mode batteryLedMode(const PowerState& power, const ChargerState& charger);

class PowerNode final : public ValueNode<PowerState> {
  public:
    explicit constexpr PowerNode(hal::PowerDevices& devices) : devices_(devices) {}
    bool init(DataBroker& broker, uint32_t now_ms);
    int onEvent(EventParam* event) override;

  private:
    hal::PowerDevices& devices_;
    uint8_t charger_ticks_ = 0U;
    SettingsState settings_{};
    void refreshDevices();
    void updateInputLimit();
    void updateChargerMode();
    hal::Status applyChargeLimits();
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
    /** Confirms the running MCUboot image once, the first time a host has configured USB. An update is always
     * installed from a host that waits for this re-enumeration, so an image whose USB never works stays unconfirmed
     * and is reverted on the next reset instead of locking the device out of USB updates. Returns the confirm
     * result when it ran, 0 otherwise. */
    int confirmAfterHost(bool usb_configured, bool auto_confirm);
    int onEvent(EventParam* event) override;
};
} // namespace dp

#endif
