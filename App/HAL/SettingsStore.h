#ifndef APP_HAL_SETTINGSSTORE_H
#define APP_HAL_SETTINGSSTORE_H
#include <cstddef>
#include <cstdint>

namespace hal {
/** A single opaque settings blob. The caller validates version, CRC and values. Main loop only. */
class SettingsStore {
  public:
    virtual bool load(uint8_t* data, std::size_t capacity, std::size_t& length) = 0;
    virtual bool save(const uint8_t* data, std::size_t length) = 0;

  protected:
    ~SettingsStore() = default;
};

} // namespace hal

#endif
