#include "Hc32I2cBus.h"
#include "bsp_i2c2.h"

namespace {
hal::Status fromBsp(int result) {
    switch (result) {
        case BSP_I2C2_OK:
            return hal::Ok;
        case BSP_I2C2_ERR_INIT:
            return hal::InitError;
        case BSP_I2C2_ERR_ARGUMENT:
            return hal::InvalidArgument;
        case BSP_I2C2_ERR_NOT_READY:
            return hal::NotReady;
        case BSP_I2C2_ERR_INIT_DEINIT:
            return hal::InitDeinitError;
        case BSP_I2C2_ERR_INIT_DEFAULTS:
            return hal::InitDefaultsError;
        case BSP_I2C2_ERR_BUS_BUSY:
            return hal::BusBusy;
        case BSP_I2C2_ERR_START_TIMEOUT:
            return hal::StartTimeout;
        case BSP_I2C2_ERR_ADDR_NACK:
            return hal::AddressNack;
        case BSP_I2C2_ERR_ADDR_TIMEOUT:
            return hal::AddressTimeout;
        case BSP_I2C2_ERR_TX_NACK:
            return hal::TransmitNack;
        case BSP_I2C2_ERR_TX_TIMEOUT:
            return hal::TransmitTimeout;
        case BSP_I2C2_ERR_RESTART_TIMEOUT:
            return hal::RestartTimeout;
        case BSP_I2C2_ERR_RX_ADDR_NACK:
            return hal::ReceiveAddressNack;
        case BSP_I2C2_ERR_RX_ADDR_TIMEOUT:
            return hal::ReceiveAddressTimeout;
        case BSP_I2C2_ERR_RX_STOP_TIMEOUT:
            return hal::ReceiveStopTimeout;
        case BSP_I2C2_ERR_STOP_TIMEOUT:
            return hal::StopTimeout;
        case BSP_I2C2_ERR_ARBITRATION_LOST:
            return hal::ArbitrationLost;
        default:
            return hal::UnknownError;
    }
}
} // namespace
namespace hal {
Status Hc32I2cBus::init() {
    return fromBsp(bsp_i2c2_init());
}
uint32_t Hc32I2cBus::recoveries() {
    return bsp_i2c2_recovery_count();
}
Status Hc32I2cBus::probe(uint8_t address) {
    return fromBsp(bsp_i2c2_probe(address));
}
Status Hc32I2cBus::write(uint8_t address, const uint8_t* data, uint32_t length) {
    return fromBsp(bsp_i2c2_write(address, data, length));
}
Status Hc32I2cBus::readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    return fromBsp(bsp_i2c2_read_reg(address, reg, data, length));
}
} // namespace hal
