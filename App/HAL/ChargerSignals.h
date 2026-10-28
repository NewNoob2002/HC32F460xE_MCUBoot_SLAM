#pragma once
namespace hal {
struct ChargerSample {
    bool interrupt_asserted = false;
    bool power_good = false;
};
/** PA4=INT#，PA5=PG（反相 ACOK#）；初始化失败时不得采样浮空引脚。 */
class ChargerSignals {
  public:
    virtual bool init() = 0;
    virtual bool read(ChargerSample& sample) = 0;

  protected:
    ~ChargerSignals() = default;
};
class Hc32ChargerSignals final : public ChargerSignals {
  public:
    bool init() override;
    bool read(ChargerSample& sample) override;
};
} // namespace hal
