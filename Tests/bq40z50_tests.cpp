#include "FakeI2cBus.h"

int main() {
    FakeI2cBus bus;
    hal::BQ40Z50 device(bus, bus.expected_address);
    hal::BqIdentity identity{};
    assert(device.readIdentity(&identity) == hal::BQ40Z50_OK);
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
    assert(device.readIdentity(&identity) == hal::BQ40Z50_ERR_PROTOCOL);
    assert(std::memcmp(&identity, &saved, sizeof(identity)) == 0);
    bus.bad_count = false;
    bus.write_result = BSP_I2C2_ERR_TX_TIMEOUT;
    const unsigned reads = bus.reads;
    assert(device.readIdentity(&identity) == BSP_I2C2_ERR_TX_TIMEOUT);
    assert(bus.reads == reads);
    bus.write_result = BSP_I2C2_OK;
    bus.read_result = BSP_I2C2_ERR_RX_STOP_TIMEOUT;
    // 每个读取阶段失败都不得将部分 identity 提交给调用方。
    for (unsigned stage = 1U; stage <= 4U; ++stage) {
        bus.reads = 0U;
        bus.fail_read_at = stage;
        assert(device.readIdentity(&identity) == BSP_I2C2_ERR_RX_STOP_TIMEOUT);
        assert(bus.reads == stage);
        assert(std::memcmp(&identity, &saved, sizeof(identity)) == 0);
    }
    assert(device.readIdentity(nullptr) == hal::BQ40Z50_ERR_ARGUMENT);
    hal::BQ40Z50 invalid(bus, 0x80U);
    assert(invalid.readIdentity(&identity) == hal::BQ40Z50_ERR_ARGUMENT);
    return 0;
}
