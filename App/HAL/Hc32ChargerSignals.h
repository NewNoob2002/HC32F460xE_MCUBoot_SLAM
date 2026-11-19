#ifndef APP_HAL_HC32CHARGERSIGNALS_H
#define APP_HAL_HC32CHARGERSIGNALS_H

#include "ChargerSignals.h"

namespace hal {
class Hc32ChargerSignals final : public ChargerSignals {
  public:
    bool init() override;
    bool read(ChargerSample& sample) override;
};
} // namespace hal

#endif
