#pragma once
#include "I2cBus.h"

namespace hal {
class HUSB238 {
  public:
    static constexpr uint8_t Address = 0x08U;
    explicit constexpr HUSB238(I2cBus& bus) : bus_(bus) {}
    int probe() {
        return bus_.probe(Address);
    }

  private:
    I2cBus& bus_;
};
/** Raw MP2762A status and ADC registers; scaling is decoded by the host (DEBUG_LINK_V1.md). */
struct Mp2762aStatus {
    uint8_t status;           // 0x13
    uint8_t fault;            // 0x14
    uint16_t battery_voltage; // 0x16..0x17, little-endian
    uint16_t system_voltage;  // 0x18..0x19
    uint16_t charge_current;  // 0x1A..0x1B
    uint16_t input_voltage;   // 0x1C..0x1D
    uint16_t input_current;   // 0x1E..0x1F
};
class MP2762A {
  public:
    static constexpr uint8_t Address = 0x5CU;
    explicit constexpr MP2762A(I2cBus& bus) : bus_(bus) {}
    int probe() {
        return bus_.probe(Address);
    }
    /** Read-only; *status changes only when every register was read. */
    int readStatus(Mp2762aStatus* status) {
        static constexpr uint8_t word_registers[] = {0x16U, 0x18U, 0x1AU, 0x1CU, 0x1EU};
        if (status == nullptr)
            return BSP_I2C2_ERR_ARGUMENT;
        Mp2762aStatus next{};
        int result = bus_.readReg(Address, 0x13U, &next.status, 1U);
        if (result == BSP_I2C2_OK)
            result = bus_.readReg(Address, 0x14U, &next.fault, 1U);
        uint16_t* const words[] = {&next.battery_voltage, &next.system_voltage, &next.charge_current,
                                   &next.input_voltage, &next.input_current};
        for (unsigned i = 0U; result == BSP_I2C2_OK && i < sizeof(word_registers); ++i) {
            uint8_t bytes[2]{};
            result = bus_.readReg(Address, word_registers[i], bytes, sizeof(bytes));
            *words[i] = static_cast<uint16_t>(bytes[0] | static_cast<uint16_t>(bytes[1] << 8U));
        }
        if (result == BSP_I2C2_OK)
            *status = next;
        return result;
    }

  private:
    I2cBus& bus_;
};
} // namespace hal
