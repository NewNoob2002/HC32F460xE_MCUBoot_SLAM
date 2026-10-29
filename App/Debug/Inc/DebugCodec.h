#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace debug {
inline constexpr std::size_t MaxPayload = 256U;
inline constexpr std::size_t MaxFrame = 276U;
inline uint16_t read16(const uint8_t* p) {
    return static_cast<uint16_t>(uint16_t{p[0]} | static_cast<uint16_t>(uint16_t{p[1]} << 8U));
}
inline uint32_t read32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8U) | (uint32_t{p[2]} << 16U) | (uint32_t{p[3]} << 24U);
}
inline void write16(uint8_t* p, uint16_t value) {
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8U);
}
inline void write32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        p[i] = static_cast<uint8_t>(value >> (8U * i));
}
struct Frame {
    uint8_t command = 0U;
    uint8_t flags = 0U;
    uint32_t sequence = 0U;
    const uint8_t* payload = nullptr;
    uint16_t length = 0U;
};
enum class ParseResult { More, Ready, BadFrame };
std::size_t encode(const Frame& frame, uint8_t* output, std::size_t capacity);
bool decode(const uint8_t* input, std::size_t length, Frame& frame);
/** One frame/error per feed; borrowed frame payload stays valid until the next feed/reset. */
class Parser {
  public:
    void reset();
    ParseResult feed(const uint8_t* data, std::size_t length, std::size_t& consumed, Frame& frame);

  private:
    std::array<uint8_t, MaxFrame> bytes_{};
    std::size_t used_ = 0U;
    std::size_t expected_ = 0U;
};
} // namespace debug
