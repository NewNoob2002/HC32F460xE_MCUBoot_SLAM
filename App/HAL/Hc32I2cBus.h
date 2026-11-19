#ifndef APP_HAL_HC32I2CBUS_H
#define APP_HAL_HC32I2CBUS_H

#include "I2cBus.h"

namespace hal {
class Hc32I2cBus final : public I2cBus {
  public:
    Status init() override;
    Status probe(uint8_t address) override;
    Status write(uint8_t address, const uint8_t* data, uint32_t length) override;
    Status readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) override;
    uint32_t recoveries() override;
};
} // namespace hal

#endif
