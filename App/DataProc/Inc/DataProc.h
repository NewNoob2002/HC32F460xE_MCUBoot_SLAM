#pragma once
#include <cstring>
#include <type_traits>
#include "BQ40Z50.h"
#include "ChargerDevices.h"
#include "ChargerSignals.h"
#include "DataBroker.h"
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
/** Battery slot 0 is the default 0x0B pack, slot 1 the alternate 0x0C pack. */
inline constexpr unsigned BatteryCount = 2U;
/** Without a pack the charger DC/DC only cycles into battery OVP, so it is suspended after this many absent ticks. */
inline constexpr uint8_t ChargerSuspendAfterTicks = 3U;
/** A pack in BQ40Z50 shutdown answers I2C only once the charger drives PACK+, so suspend is lifted periodically.
 * Window length is a first guess that HIL must confirm against a sleeping pack. */
inline constexpr uint8_t ChargerWakeIntervalTicks = 60U;
inline constexpr uint8_t ChargerWakeWindowTicks = 5U;
enum class ChargerMode : uint8_t { Active, Suspended, WakeProbe };
/** Slot of the pack charged through J2 (BQ40Z50 default address 0x0B). Slot 1 (0x0C) is an output-only pack that
 * is monitored but never charged. */
inline constexpr unsigned ChargedPackSlot = 0U;
/** Hard 2S charge-voltage ceiling, independent of settings and pack requests. */
inline constexpr uint16_t ChargeVoltageCeilingMv = 8400U;
/** Input current the source allows, from the HUSB238 PD contract or the Type-C 5 V advertisement. An unreadable
 * HUSB238 or a default-current source falls back to USB 2.0's 500 mA. */
uint16_t sourceCurrentLimitMa(int husb238_result, const hal::Husb238Status& status);
/** The MP2762A input limit leaves headroom for the main board and HUSB238 on the same input and for the MP2762A
 * IIN_LIM tolerance (+/-5% at 1.5 A, datasheet Rev 1.1). */
inline constexpr unsigned SourceHeadroomPercent = 90U;

struct PowerState {
    int i2c_init_result = 0;
    int bq_default_probe_result = 0;
    int bq_alternate_probe_result = 0;
    int husb238_probe_result = 0;
    int mp2762a_probe_result = 0;
    int bq_alternate_identity_result = 0;
    hal::BqIdentity bq_alternate_identity{};
    // Not read until the first BatteryPeriodMs tick; nonzero result keeps the previous status invalid.
    int bq_status_result[BatteryCount] = {BSP_I2C2_ERR_NOT_READY, BSP_I2C2_ERR_NOT_READY};
    hal::BqStatus bq_status[BatteryCount]{};
    int mp2762a_status_result = BSP_I2C2_ERR_NOT_READY;
    hal::Mp2762aStatus mp2762a_status{};
    ChargerMode charger_mode = ChargerMode::Active;
    int charger_control_result = BSP_I2C2_ERR_NOT_READY;
    // Last limits computed from the charged pack's request and the settings; zero while charging is not allowed.
    bool charge_allowed = false;
    uint16_t charge_target_mv = 0U;
    uint16_t charge_target_ma = 0U;
    int husb238_status_result = BSP_I2C2_ERR_NOT_READY;
    hal::Husb238Status husb238_status{};
    uint16_t source_limit_ma = 0U; // what the source allows
    uint16_t input_limit_ma = 0U;  // what was written to REG00H
    int mp2762a_config_result = BSP_I2C2_ERR_NOT_READY;
    hal::Mp2762aConfig mp2762a_config{};
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
    explicit constexpr PowerNode(hal::I2cBus& bus) : bus_(bus) {}
    bool init(DataBroker& broker, uint32_t now_ms);
    int onEvent(EventParam* event) override;

  private:
    hal::I2cBus& bus_;
    uint8_t charger_ticks_ = 0U;
    SettingsState settings_{};
    void refreshDevices();
    void updateInputLimit();
    void updateChargerMode();
    int applyChargeLimits(hal::MP2762A& charger);
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
