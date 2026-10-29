#include <cstring>
#include "Settings.h"
#include "fw_update/protocol.h"

namespace {
uint32_t read32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8U) | (uint32_t{p[2]} << 16U) | (uint32_t{p[3]} << 24U);
}
void write32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        p[i] = static_cast<uint8_t>(value >> (8U * i));
}
} // namespace
namespace dp {
bool SettingsNode::init(DataBroker& broker) {
    if (DataNode::init(broker, SettingsName, EVENT_PULL) != RES_OK)
        return false;
    state_ = {};
    uint8_t blob[BlobSize]{};
    std::size_t length = 0U;
    if (store_.load(blob, sizeof(blob), length) && length == sizeof(blob)
        && std::memcmp(blob, "SET1\x01\x05\0\0", 8U) == 0 && fw_protocol_crc32(blob, 28U) == read32(blob + 28U)) {
        for (std::size_t i = 0U; i < SettingTable.size(); ++i) {
            const auto raw = read32(blob + 8U + i * 4U);
            int32_t value = 0;
            std::memcpy(&value, &raw, sizeof(value));
            if (value >= SettingTable[i].min && value <= SettingTable[i].max)
                state_.values[i] = value;
        }
    }
    return true;
}
SettingResult SettingsNode::get(uint16_t key, int32_t& value) const {
    for (std::size_t i = 0U; i < SettingTable.size(); ++i) {
        if (SettingTable[i].key == key) {
            value = state_.values[i];
            return SettingResult::Ok;
        }
    }
    return SettingResult::UnknownKey;
}
SettingResult SettingsNode::persist(const SettingsState& next) {
    if (next.values == state_.values)
        return SettingResult::Ok;
    uint8_t blob[BlobSize]{};
    std::memcpy(blob, "SET1\x01\x05\0\0", 8U);
    for (std::size_t i = 0U; i < SettingTable.size(); ++i)
        write32(blob + 8U + i * 4U, static_cast<uint32_t>(next.values[i]));
    write32(blob + 28U, fw_protocol_crc32(blob, 28U));
    if (!store_.save(blob, sizeof(blob)))
        return SettingResult::StorageError;
    state_ = next;
    const SettingsState snapshot = state_;
    (void)publish(&snapshot, sizeof(snapshot));
    return SettingResult::Ok;
}
SettingResult SettingsNode::set(uint16_t key, int32_t value) {
    for (std::size_t i = 0U; i < SettingTable.size(); ++i) {
        if (SettingTable[i].key != key)
            continue;
        if (value < SettingTable[i].min || value > SettingTable[i].max)
            return SettingResult::OutOfRange;
        auto next = state_;
        next.values[i] = value;
        return persist(next);
    }
    return SettingResult::UnknownKey;
}
SettingResult SettingsNode::reset() {
    return persist(SettingsState{});
}
int SettingsNode::onEvent(EventParam* event) {
    return replyPull(event, &state_, sizeof(state_));
}
} // namespace dp
