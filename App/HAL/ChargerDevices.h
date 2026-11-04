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
/** A REG08H write did not read back as written. Outside the BSP_I2C2_* and BQ40Z50_* ranges. */
inline constexpr int Mp2762aErrVerify = -24;
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

    /** Suspend turns the DC/DC off (REG08H SUSP_EN) and stops charging (CHG_EN); resume restores DC/DC and sets
     * CHG_EN to `charge`. REG_RST/WTD_RST are self-clearing commands and always written as 0. */
    int setSuspended(bool suspended, bool charge = true) {
        constexpr uint8_t Commands = 0xC0U; // REG_RST | WTD_RST
        constexpr uint8_t SuspEn = 0x08U;
        constexpr uint8_t ChgEn = 0x10U;
        const uint8_t value = suspended ? SuspEn : charge ? ChgEn : 0U;
        return updateBits(0x08U, Commands | SuspEn | ChgEn, value);
    }
    /** REG04H VBATT_REG: 7425 mV + 25 mV steps up to 9000 mV; VRECH_OS (bit 0) is kept. */
    int setChargeVoltage(uint16_t millivolts) {
        if (millivolts < MinChargeMv || millivolts > 9000U)
            return BSP_I2C2_ERR_ARGUMENT;
        return updateBits(0x04U, 0x7EU, static_cast<uint8_t>(((millivolts - MinChargeMv) / 25U) << 1U));
    }
    /** REG02H ICC: 50 mA steps, 0..6350 mA; rounds down. */
    int setChargeCurrent(uint16_t milliamps) {
        return updateBits(0x02U, 0x7FU, stepOf50(milliamps));
    }
    /** REG00H IIN_LIM1 (RS = 10 mOhm): 50 mA steps, 0..6350 mA; rounds down. */
    int setInputCurrentLimit(uint16_t milliamps) {
        return updateBits(0x00U, 0x7FU, stepOf50(milliamps));
    }
    static constexpr uint16_t MinChargeMv = 7425U;

  private:
    I2cBus& bus_;
    static uint8_t stepOf50(uint16_t milliamps) {
        const unsigned step = milliamps / 50U;
        return static_cast<uint8_t>(step > 0x7FU ? 0x7FU : step);
    }
    /** Read-modify-write of `mask` bits (datasheet MP2762A Rev 1.1); writes only on a difference and verifies the
     * read-back so a register that did not take the value is reported, not assumed. */
    int updateBits(uint8_t reg, uint8_t mask, uint8_t value) {
        uint8_t current = 0U;
        int result = bus_.readReg(Address, reg, &current, 1U);
        if (result != BSP_I2C2_OK)
            return result;
        const auto wanted = static_cast<uint8_t>((current & ~mask) | (value & mask));
        if (wanted == current)
            return BSP_I2C2_OK;
        const uint8_t frame[2] = {reg, wanted};
        result = bus_.write(Address, frame, sizeof(frame));
        if (result != BSP_I2C2_OK)
            return result;
        uint8_t readback = 0U;
        result = bus_.readReg(Address, reg, &readback, 1U);
        if (result != BSP_I2C2_OK)
            return result;
        return readback == wanted ? BSP_I2C2_OK : Mp2762aErrVerify;
    }
};
} // namespace hal
