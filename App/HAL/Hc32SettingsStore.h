#ifndef APP_HAL_HC32SETTINGSSTORE_H
#define APP_HAL_HC32SETTINGSSTORE_H

#include "SettingsStore.h"

namespace hal {
class Hc32SettingsStore final : public SettingsStore {
  public:
    bool init();
    bool load(uint8_t* data, std::size_t capacity, std::size_t& length) override;
    bool save(const uint8_t* data, std::size_t length) override;

  private:
    bool ready_ = false;
};
} // namespace hal

#endif
