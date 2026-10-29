#include "BQ40Z50.h"
#include <cstring>

namespace hal {
#define BQ40Z50_MANUFACTURER_ACCESS_REG 0x00U
#define BQ40Z50_MANUFACTURER_DATA_REG   0x23U
#define BQ40Z50_FIRMWARE_VERSION_SIZE   11U

static uint16_t read_u16_le(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint16_t read_u16_be(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

int BQ40Z50::readManufacturerBlock(uint16_t command, uint8_t* payload, uint8_t payload_size) {
    const uint8_t request[] = {
        BQ40Z50_MANUFACTURER_ACCESS_REG,
        (uint8_t)command,
        (uint8_t)(command >> 8U),
    };
    uint8_t response[BQ40Z50_FIRMWARE_VERSION_SIZE + 1U];

    if (payload == nullptr || payload_size == 0U || payload_size > BQ40Z50_FIRMWARE_VERSION_SIZE)
        return BQ40Z50_ERR_ARGUMENT;

    int result = bus_.write(address_, request, sizeof(request));
    if (result != BSP_I2C2_OK)
        return result;
    result = bus_.readReg(address_, BQ40Z50_MANUFACTURER_DATA_REG, response, (uint32_t)payload_size + 1U);
    if (result != BSP_I2C2_OK)
        return result;
    if (response[0] != payload_size)
        return BQ40Z50_ERR_PROTOCOL;

    memcpy(payload, &response[1], payload_size);
    return BQ40Z50_OK;
}

int BQ40Z50::readWord(uint8_t command, uint16_t* value) {
    uint8_t word[2];
    const int result = bus_.readReg(address_, command, word, sizeof(word));
    if (result != BSP_I2C2_OK)
        return result;
    *value = read_u16_le(word);
    return BQ40Z50_OK;
}

int BQ40Z50::readStatus(BqStatus* status) {
    // Smart Battery Data Specification 1.1 command codes, all SMBus read-word.
    struct Field {
        uint8_t command;
        uint16_t BqStatus::* member;
    };
    static constexpr Field fields[] = {
        {0x03U, &BqStatus::battery_mode},        {0x09U, &BqStatus::voltage_mv},
        {0x08U, &BqStatus::temperature_dk},      {0x0DU, &BqStatus::relative_soc_percent},
        {0x0FU, &BqStatus::remaining_capacity},  {0x10U, &BqStatus::full_charge_capacity},
        {0x15U, &BqStatus::charging_voltage_mv}, {0x14U, &BqStatus::charging_current_ma},
        {0x16U, &BqStatus::battery_status},      {0x17U, &BqStatus::cycle_count},
    };
    BqStatus next{};
    uint16_t current = 0U;

    if (address_ > 0x7FU || status == nullptr)
        return BQ40Z50_ERR_ARGUMENT;
    for (const auto& field : fields) {
        const int result = readWord(field.command, &(next.*field.member));
        if (result != BQ40Z50_OK)
            return result;
    }
    const int result = readWord(0x0AU, &current);
    if (result != BQ40Z50_OK)
        return result;
    next.current_ma = static_cast<int16_t>(current);
    *status = next;
    return BQ40Z50_OK;
}

int BQ40Z50::readIdentity(BqIdentity* identity) {
    uint8_t device_type[2];
    uint8_t firmware[BQ40Z50_FIRMWARE_VERSION_SIZE];
    uint8_t hardware_version[2];
    uint8_t chemistry_id[2];

    if (address_ > 0x7FU || identity == nullptr)
        return BQ40Z50_ERR_ARGUMENT;

    int result = readManufacturerBlock(0x0001U, device_type, sizeof(device_type));
    if (result != BQ40Z50_OK)
        return result;
    result = readManufacturerBlock(0x0002U, firmware, sizeof(firmware));
    if (result != BQ40Z50_OK)
        return result;
    result = readManufacturerBlock(0x0003U, hardware_version, sizeof(hardware_version));
    if (result != BQ40Z50_OK)
        return result;
    result = readManufacturerBlock(0x0006U, chemistry_id, sizeof(chemistry_id));
    if (result != BQ40Z50_OK)
        return result;

    identity->device_type = read_u16_le(device_type);
    identity->firmware.device_number = read_u16_be(&firmware[0]);
    identity->firmware.version = read_u16_be(&firmware[2]);
    identity->firmware.build = read_u16_be(&firmware[4]);
    identity->firmware.firmware_type = firmware[6];
    identity->firmware.impedance_track_version = read_u16_be(&firmware[7]);
    identity->firmware.reserved = firmware[9];
    identity->firmware.execution_context = firmware[10];
    identity->hardware_version = read_u16_le(hardware_version);
    identity->chemistry_id = read_u16_le(chemistry_id);
    return BQ40Z50_OK;
}

} // namespace hal
