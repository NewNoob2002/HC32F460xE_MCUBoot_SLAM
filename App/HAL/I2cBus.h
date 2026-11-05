#ifndef APP_HAL_I2CBUS_H
#define APP_HAL_I2CBUS_H
#include <cstdint>
#include "Status.h"

namespace hal {
/** Callers own the bus and devices and must not delete through the base class. */
class I2cBus {
  public:
    virtual Status init() = 0;
    virtual Status probe(uint8_t address) = 0;
    virtual Status write(uint8_t address, const uint8_t* data, uint32_t length) = 0;
    virtual Status readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) = 0;
    /** Bus clears performed after a transfer could not START; 0 for buses without recovery. */
    virtual uint32_t recoveries() {
        return 0U;
    }

  protected:
    ~I2cBus() = default;
};
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
