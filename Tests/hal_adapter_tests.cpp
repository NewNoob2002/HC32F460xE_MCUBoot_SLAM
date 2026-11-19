#include <array>
#include <cassert>
#include <limits>

#include "Hc32I2cBus.h"
#include "bsp_i2c2.h"

namespace {
int result = BSP_I2C2_OK;
}

extern "C" {
int bsp_i2c2_init(void) {
    return result;
}
int bsp_i2c2_probe(uint8_t address) {
    assert(address == 0x08U);
    return result;
}
int bsp_i2c2_write(uint8_t address, const uint8_t* data, uint32_t length) {
    assert(address == 0x08U && data != nullptr && length == 1U);
    return result;
}
int bsp_i2c2_read_reg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    assert(address == 0x08U && reg == 0x01U && data != nullptr && length == 1U);
    return result;
}
uint32_t bsp_i2c2_recovery_count(void) {
    return 7U;
}
}

int main() {
    struct Case {
        int bsp;
        hal::Status expected;
    };
    constexpr Case cases[] = {
        {BSP_I2C2_OK, hal::Ok},
        {BSP_I2C2_ERR_INIT, hal::InitError},
        {BSP_I2C2_ERR_ARGUMENT, hal::InvalidArgument},
        {BSP_I2C2_ERR_NOT_READY, hal::NotReady},
        {BSP_I2C2_ERR_INIT_DEINIT, hal::InitDeinitError},
        {BSP_I2C2_ERR_INIT_DEFAULTS, hal::InitDefaultsError},
        {BSP_I2C2_ERR_BUS_BUSY, hal::BusBusy},
        {BSP_I2C2_ERR_START_TIMEOUT, hal::StartTimeout},
        {BSP_I2C2_ERR_ADDR_NACK, hal::AddressNack},
        {BSP_I2C2_ERR_ADDR_TIMEOUT, hal::AddressTimeout},
        {BSP_I2C2_ERR_TX_NACK, hal::TransmitNack},
        {BSP_I2C2_ERR_TX_TIMEOUT, hal::TransmitTimeout},
        {BSP_I2C2_ERR_RESTART_TIMEOUT, hal::RestartTimeout},
        {BSP_I2C2_ERR_RX_ADDR_NACK, hal::ReceiveAddressNack},
        {BSP_I2C2_ERR_RX_ADDR_TIMEOUT, hal::ReceiveAddressTimeout},
        {BSP_I2C2_ERR_RX_STOP_TIMEOUT, hal::ReceiveStopTimeout},
        {BSP_I2C2_ERR_STOP_TIMEOUT, hal::StopTimeout},
        {BSP_I2C2_ERR_ARBITRATION_LOST, hal::ArbitrationLost},
        {std::numeric_limits<int>::min(), hal::UnknownError},
        {1, hal::UnknownError},
    };
    hal::Hc32I2cBus bus;
    uint8_t byte = 0U;
    for (const auto& test : cases) {
        result = test.bsp;
        assert(bus.init() == test.expected);
        assert(bus.probe(0x08U) == test.expected);
        assert(bus.write(0x08U, &byte, 1U) == test.expected);
        assert(bus.readReg(0x08U, 0x01U, &byte, 1U) == test.expected);
    }
    assert(bus.recoveries() == 7U);
}
