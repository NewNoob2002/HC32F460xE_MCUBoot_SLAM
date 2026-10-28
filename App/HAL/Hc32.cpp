#include "ChargerSignals.h"
#include "I2cBus.h"
#include "bsp_charger_signals.h"

namespace hal {
int Hc32I2cBus::init() {
    return bsp_i2c2_init();
}
int Hc32I2cBus::probe(uint8_t address) {
    return bsp_i2c2_probe(address);
}
int Hc32I2cBus::write(uint8_t address, const uint8_t* data, uint32_t length) {
    return bsp_i2c2_write(address, data, length);
}
int Hc32I2cBus::readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    return bsp_i2c2_read_reg(address, reg, data, length);
}
bool Hc32ChargerSignals::init() {
    return bsp_charger_signals_init();
}
bool Hc32ChargerSignals::read(ChargerSample& sample) {
    bsp_charger_signals signals{};
    if (!bsp_charger_signals_read(&signals))
        return false;
    sample = {signals.interrupt_asserted, signals.power_good};
    return true;
}
} // namespace hal
