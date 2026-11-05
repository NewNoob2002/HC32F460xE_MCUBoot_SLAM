#include "FakeI2cBus.h"

int main() {
    FakeI2cBus bus;
    hal::BQ40Z50 device(bus, bus.expected_address);
    hal::BqIdentity identity{};
    assert(device.readIdentity(&identity) == hal::Ok);
    assert(bus.reads == 4U);
    assert(identity.device_type == 0x4500U);
    assert(identity.firmware.device_number == 0x4500U);
    assert(identity.firmware.version == 0x0106U);
    assert(identity.firmware.build == 0x0024U);
    assert(identity.firmware.firmware_type == 0x00U);
    assert(identity.firmware.impedance_track_version == 0x0385U);
    assert(identity.firmware.reserved == 0x02U);
    assert(identity.firmware.execution_context == 0x00U);
    assert(identity.hardware_version == 0x000CU);
    assert(identity.chemistry_id == 0x2107U);
    const auto saved = identity;

    bus.bad_count = true;
    assert(device.readIdentity(&identity) == hal::ProtocolError);
    assert(std::memcmp(&identity, &saved, sizeof(identity)) == 0);
    bus.bad_count = false;
    bus.write_result = hal::TransmitTimeout;
    const unsigned reads = bus.reads;
    assert(device.readIdentity(&identity) == hal::TransmitTimeout);
    assert(bus.reads == reads);
    bus.write_result = hal::Ok;
    bus.read_result = hal::ReceiveStopTimeout;
    // Failure at any read stage must not commit a partial identity to the caller.
    for (unsigned stage = 1U; stage <= 4U; ++stage) {
        bus.reads = 0U;
        bus.fail_read_at = stage;
        assert(device.readIdentity(&identity) == hal::ReceiveStopTimeout);
        assert(bus.reads == stage);
        assert(std::memcmp(&identity, &saved, sizeof(identity)) == 0);
    }
    assert(device.readIdentity(nullptr) == hal::BatteryInvalidArgument);
    hal::BQ40Z50 invalid(bus, 0x80U);
    assert(invalid.readIdentity(&identity) == hal::BatteryInvalidArgument);

    // SBS status: little-endian words, signed current, all-or-nothing update.
    bus.sbs = true;
    bus.read_result = hal::AddressNack;
    bus.sbs_words[0x03] = 0x6001U;
    bus.sbs_words[0x08] = 3015U;
    bus.sbs_words[0x09] = 7920U;
    bus.sbs_words[0x0A] = 0xFB50U; // -1200 mA
    bus.sbs_words[0x0D] = 76U;
    bus.sbs_words[0x0F] = 2340U;
    bus.sbs_words[0x10] = 3100U;
    bus.sbs_words[0x14] = 2000U;
    bus.sbs_words[0x15] = 8400U;
    bus.sbs_words[0x16] = 0x00C0U;
    bus.sbs_words[0x17] = 12U;
    hal::BqStatus status{};
    bus.reads = 0U;
    assert(device.readStatus(&status) == hal::Ok && bus.reads == 11U);
    assert(status.battery_mode == 0x6001U && status.voltage_mv == 7920U && status.current_ma == -1200);
    assert(status.temperature_dk == 3015U && status.relative_soc_percent == 76U);
    assert(status.remaining_capacity == 2340U && status.full_charge_capacity == 3100U);
    assert(status.charging_voltage_mv == 8400U && status.charging_current_ma == 2000U);
    assert(status.battery_status == 0x00C0U && status.cycle_count == 12U);
    const auto saved_status = status;
    for (const int reg : {0x03, 0x10, 0x0A}) {
        bus.sbs_fail_reg = reg;
        assert(device.readStatus(&status) == hal::AddressNack);
        assert(std::memcmp(&status, &saved_status, sizeof(status)) == 0);
    }
    assert(device.readStatus(nullptr) == hal::BatteryInvalidArgument);
    return 0;
}
