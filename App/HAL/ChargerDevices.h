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
class MP2762A {
  public:
    static constexpr uint8_t Address = 0x5CU;
    explicit constexpr MP2762A(I2cBus& bus) : bus_(bus) {}
    int probe() {
        return bus_.probe(Address);
    }

  private:
    I2cBus& bus_;
};
} // namespace hal
