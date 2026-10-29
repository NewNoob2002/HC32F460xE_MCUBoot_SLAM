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
};
inline constexpr std::array<SettingSpec, 5> SettingTable{{
    {1U, "charge_enable", 1U, 0U, 0, 1, 0},
    {2U, "charge_current_limit", 0U, 1U, 100, 2000, 500},
    {3U, "charge_voltage_limit", 0U, 2U, 12000, 16800, 16800},
    {4U, "input_current_limit", 0U, 1U, 100, 3000, 1500},
    {5U, "telemetry_period", 0U, 3U, 100, 5000, 1000},
}};
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
    SettingResult persist(const SettingsState& next);
};
} // namespace dp
