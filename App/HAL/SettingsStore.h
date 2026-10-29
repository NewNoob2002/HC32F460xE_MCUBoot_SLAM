#pragma once
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
class Hc32SettingsStore final : public SettingsStore {
  public:
    bool init();
    bool load(uint8_t* data, std::size_t capacity, std::size_t& length) override;
    bool save(const uint8_t* data, std::size_t length) override;

  private:
    bool ready_ = false;
};
} // namespace hal
