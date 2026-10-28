#pragma once
#include <cstdint>
#include "bsp_i2c2.h"

namespace hal {
/** 返回 BSP_I2C2_* 结果；调用者持有总线及设备，无基类删除。 */
class I2cBus {
  public:
    virtual int init() = 0;
    virtual int probe(uint8_t address) = 0;
    virtual int write(uint8_t address, const uint8_t* data, uint32_t length) = 0;
    virtual int readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) = 0;

  protected:
    ~I2cBus() = default;
};
class Hc32I2cBus final : public I2cBus {
  public:
    int init() override;
    int probe(uint8_t address) override;
    int write(uint8_t address, const uint8_t* data, uint32_t length) override;
    int readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) override;
};
} // namespace hal
