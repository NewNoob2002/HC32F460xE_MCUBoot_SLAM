#pragma once
#include <array>
#include <cassert>
#include <cstring>
#include "BQ40Z50.h"

/** Fake bus for device protocol tests; no BSP link-time replacements. */
class FakeI2cBus final : public hal::I2cBus {
  public:
    int init_result = BSP_I2C2_OK;
    std::array<int, 4> probe_results{};
    std::array<uint8_t, 4> probes{};
    unsigned probe_count = 0U;
    unsigned init_count = 0U;
    unsigned writes = 0U;
    unsigned reads = 0U;
    int write_result = BSP_I2C2_OK;
    int read_result = BSP_I2C2_OK;
    unsigned fail_read_at = 0U;
    bool bad_count = false;
    uint8_t expected_address = hal::BQ40Z50::AlternateAddress;
    uint16_t command = 0U;
    int init() override {
        ++init_count;
        return init_result;
    }
    int probe(uint8_t address) override {
        assert(probe_count < probes.size());
        probes[probe_count] = address;
        return probe_results[probe_count++];
    }
    int write(uint8_t address, const uint8_t* data, uint32_t length) override {
        assert(address == expected_address && data && length == 3U && data[0] == 0x00U);
        command = static_cast<uint16_t>(data[1] | static_cast<uint16_t>(data[2] << 8U));
        ++writes;
        return write_result;
    }
    int readReg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) override {
        static constexpr uint8_t firmware[] = {0x45U, 0x00U, 0x01U, 0x06U, 0x00U, 0x24U,
                                               0x00U, 0x03U, 0x85U, 0x02U, 0x00U};
        assert(address == expected_address && reg == 0x23U && data);
        ++reads;
        if (read_result != BSP_I2C2_OK && (fail_read_at == 0U || reads == fail_read_at))
            return read_result;
        const uint8_t* payload = nullptr;
        uint8_t word[2]{};
        uint8_t size = 2U;
        switch (command) {
            case 1U:
                word[0] = 0x00U;
                word[1] = 0x45U;
                break;
            case 2U:
                payload = firmware;
                size = sizeof(firmware);
                break;
            case 3U:
                word[0] = 0x0CU;
                word[1] = 0x00U;
                break;
            case 6U:
                word[0] = 0x07U;
                word[1] = 0x21U;
                break;
            default:
                assert(false);
        }
        if (!payload)
            payload = word;
        assert(length == static_cast<uint32_t>(size) + 1U);
        data[0] = bad_count ? 0U : size;
        std::memcpy(data + 1, payload, size);
        return BSP_I2C2_OK;
    }
};
