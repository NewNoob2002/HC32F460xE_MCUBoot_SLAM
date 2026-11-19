#ifndef APP_HAL_CHARGERSIGNALS_H
#define APP_HAL_CHARGERSIGNALS_H
namespace hal {
struct ChargerSample {
    bool interrupt_asserted = false;
    bool power_good = false;
};
/** PA4=INT#, PA5=PG (inverted ACOK#); do not sample floating pins after initialization fails. */
class ChargerSignals {
  public:
    virtual bool init() = 0;
    virtual bool read(ChargerSample& sample) = 0;

  protected:
    ~ChargerSignals() = default;
};

} // namespace hal

#endif
