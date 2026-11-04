#pragma once
#include <array>
#include "DataBroker.h"
#include "SettingsStore.h"

namespace dp {
inline constexpr char SettingsName[] = "Settings";
struct SettingSpec {
    uint16_t key;
    const char* name;
    uint8_t type;
    uint8_t unit;
    int32_t min;
    int32_t max;
    int32_t initial;
    /** CONFIG_LIST flags: bit0 persisted, bit1 requires unlock, bit2 applied to hardware. */
    uint8_t flags;
};
inline constexpr uint8_t SettingPersisted = 1U << 0U;
inline constexpr uint8_t SettingRequiresUnlock = 1U << 1U;
inline constexpr uint8_t SettingApplied = 1U << 2U;
inline constexpr uint16_t LedOverrideKey = 6U;
/** led_override values: 0 follows the normal pattern; 1..4 hold the LEDs off, red, blue or red+blue. */
enum class LedOverride : int32_t { Auto = 0, Off, Red, Blue, RedBlue };
inline constexpr std::array<SettingSpec, 6> SettingTable{{
    {1U, "charge_enable", 1U, 0U, 0, 1, 0, SettingPersisted | SettingRequiresUnlock},
    {2U, "charge_current_limit", 0U, 1U, 100, 2000, 500, SettingPersisted | SettingRequiresUnlock},
    {3U, "charge_voltage_limit", 0U, 2U, 12000, 16800, 16800, SettingPersisted | SettingRequiresUnlock},
    {4U, "input_current_limit", 0U, 1U, 100, 3000, 1500, SettingPersisted | SettingRequiresUnlock},
    {5U, "telemetry_period", 0U, 3U, 100, 5000, 1000, SettingPersisted | SettingRequiresUnlock},
    // Runtime-only so a power-measurement override can never survive a reset and ship with dark LEDs.
    {LedOverrideKey, "led_override", 0U, 0U, 0, 4, 0, SettingRequiresUnlock | SettingApplied},
}};
/** Settings blob v1 stores exactly the leading persisted entries; runtime-only entries must follow them. */
inline constexpr std::size_t PersistedSettingCount = 5U;
constexpr bool persistedEntriesLead() {
    for (std::size_t i = 0U; i < SettingTable.size(); ++i)
        if (((SettingTable[i].flags & SettingPersisted) != 0U) != (i < PersistedSettingCount))
            return false;
    return true;
}
static_assert(persistedEntriesLead(), "persisted settings must be the first PersistedSettingCount entries");
struct SettingsState {
    std::array<int32_t, SettingTable.size()> values{};
    constexpr SettingsState() {
        for (std::size_t i = 0U; i < SettingTable.size(); ++i)
            values[i] = SettingTable[i].initial;
    }
};
enum class SettingResult { Ok, UnknownKey, OutOfRange, StorageError };
class SettingsNode final : public DataNode {
  public:
    static constexpr std::size_t BlobSize = 32U;
    explicit constexpr SettingsNode(hal::SettingsStore& store) : store_(store) {}
    bool init(DataBroker& broker);
    SettingResult get(uint16_t key, int32_t& value) const;
    SettingResult set(uint16_t key, int32_t value);
    SettingResult reset();
    int onEvent(EventParam* event) override;

  private:
    hal::SettingsStore& store_;
    SettingsState state_{};
    SettingResult commit(const SettingsState& next);
};
} // namespace dp
