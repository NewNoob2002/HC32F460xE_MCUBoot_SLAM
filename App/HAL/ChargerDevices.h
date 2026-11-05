#pragma once
#include "I2cBus.h"

namespace hal {
/** Raw HUSB238 PD_STATUS0/1 (Hynetek HUSB238 Register Information Rev 1.1, 0x00..0x01). */
struct Husb238Status {
    uint8_t pd_status0; // [7:4] PD_SRC_VOLTAGE, [3:0] PD_SRC_CURRENT of the explicit contract
    uint8_t pd_status1; // bit6 ATTACH, [1:0] 5V_CURRENT of a Type-C (no PD) source
};
class HUSB238 {
  public:
    static constexpr uint8_t Address = 0x08U;
    explicit constexpr HUSB238(I2cBus& bus) : bus_(bus) {}
    int probe() {
        return bus_.probe(Address);
    }
    /** Read-only; *status changes only on success. */
    int readStatus(Husb238Status* status) {
        if (status == nullptr)
            return BSP_I2C2_ERR_ARGUMENT;
        uint8_t bytes[2]{};
        const int result = bus_.readReg(Address, 0x00U, bytes, sizeof(bytes));
        if (result == BSP_I2C2_OK)
            *status = {bytes[0], bytes[1]};
        return result;
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
/** MP2762A control registers as read back after the Power node applied them, plus the die temperature ADC. */
struct Mp2762aConfig {
    uint8_t input_limit;    // REG00H IIN_LIM1, 50 mA/LSB in bits 6:0
    uint8_t input_limit2;   // REG0FH IIN_LIM2, 50 mA/LSB in bits 6:0
    uint8_t charge_current; // REG02H ICC, 50 mA/LSB in bits 6:0
    uint8_t charge_voltage; // REG04H VBATT_REG = 7425 mV + 25 mV x bits 6:1
    uint8_t control;        // REG08H, bit3 SUSP_EN, bit4 CHG_EN
    uint16_t junction_temp; // REG24..25H, 10-bit code in bits 15:6; T(C) = (903 - code) / 2.578
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

    /** Read-only; *config changes only when every register was read. */
    int readConfig(Mp2762aConfig* config) {
        if (config == nullptr)
            return BSP_I2C2_ERR_ARGUMENT;
        Mp2762aConfig next{};
        uint8_t* const bytes[] = {&next.input_limit, &next.input_limit2, &next.charge_current, &next.charge_voltage,
                                  &next.control};
        static constexpr uint8_t registers[] = {0x00U, 0x0FU, 0x02U, 0x04U, 0x08U};
        int result = BSP_I2C2_OK;
        for (unsigned i = 0U; result == BSP_I2C2_OK && i < sizeof(registers); ++i)
            result = bus_.readReg(Address, registers[i], bytes[i], 1U);
        uint8_t temp[2]{};
        if (result == BSP_I2C2_OK)
            result = bus_.readReg(Address, 0x24U, temp, sizeof(temp));
        next.junction_temp = static_cast<uint16_t>(temp[0] | static_cast<uint16_t>(temp[1] << 8U));
        if (result == BSP_I2C2_OK)
            *config = next;
        return result;
    }
    /** Suspend turns the DC/DC off (REG08H SUSP_EN) and stops charging (CHG_EN); resume restores DC/DC and sets
     * CHG_EN to `charge`. REG_RST/WTD_RST are self-clearing commands and always written as 0. */
    int setSuspended(bool suspended, bool charge) {
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
    /** REG02H ICC rounds down in 50 mA steps. REG03H IPRE stays at its 180 mA minimum (Rev 1.1, p33);
     * lower requests cannot be honored across automatic phase changes and must leave charging disabled. */
    int setChargeCurrent(uint16_t milliamps) {
        if (milliamps < MinChargeMa)
            return BSP_I2C2_ERR_ARGUMENT;
        int result = updateBits(0x03U, 0xF0U, 0x30U);
        if (result == BSP_I2C2_OK)
            result = updateBits(0x02U, 0x7FU, stepOf50(milliamps));
        return result;
    }
    /** REG00H IIN_LIM1 and REG0FH IIN_LIM2 (RS = 10 mOhm): 50 mA steps, 0..6350 mA; rounds down. Both get the same
     * value: by default IIN_LIM2 (1.5 A OTP) may run for 700 us of every 1.6 ms (REG10H/REG11H), which lifts the
     * average above IIN_LIM1 and, on HIL, over a 12 V / 1.25 A PD contract. */
    int setInputCurrentLimit(uint16_t milliamps) {
        const uint8_t step = stepOf50(milliamps);
        int result = updateBits(0x0FU, 0x7FU, step);
        if (result == BSP_I2C2_OK)
            result = updateBits(0x00U, 0x7FU, step);
        return result;
    }
    static constexpr uint16_t MinChargeMv = 7425U;
    static constexpr uint16_t MinChargeMa = 180U;

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
