#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bq40z50.h"
#include "bsp_i2c2.h"

static uint16_t g_command;
static unsigned int g_reads;
static int g_bus_result;
static bool g_bad_count;

int bsp_i2c2_write(uint8_t address, const uint8_t* data, uint32_t length) {
    assert(address == BQ40Z50_I2C_ADDRESS);
    assert(data != NULL);
    assert(length == 3U);
    assert(data[0] == 0x00U);
    g_command = ((uint16_t)data[1] << 8U) | data[2];
    return g_bus_result;
}

int bsp_i2c2_read_reg(uint8_t address, uint8_t reg, uint8_t* data, uint32_t length) {
    static const uint8_t firmware[] = {0x50U, 0x40U, 0x34U, 0x12U, 0x78U, 0x56U, 0x02U, 0xCDU, 0xABU, 0x00U, 0x01U};
    const uint8_t* payload = NULL;
    uint8_t word[2];
    uint8_t payload_length = 0U;

    assert(address == BQ40Z50_I2C_ADDRESS);
    assert(reg == 0x23U);
    assert(data != NULL);
    if (g_bus_result != BSP_I2C2_OK)
        return g_bus_result;
    switch (g_command) {
        case 0x0001U:
            word[0] = 0x50U;
            word[1] = 0x45U;
            payload = word;
            payload_length = 2U;
            break;
        case 0x0002U:
            payload = firmware;
            payload_length = sizeof(firmware);
            break;
        case 0x0003U:
            word[0] = 0x22U;
            word[1] = 0x11U;
            payload = word;
            payload_length = 2U;
            break;
        case 0x0006U:
            word[0] = 0x44U;
            word[1] = 0x33U;
            payload = word;
            payload_length = 2U;
            break;
        default:
            assert(false);
    }
    assert(length == (uint32_t)payload_length + 1U);
    data[0] = g_bad_count ? 0U : payload_length;
    memcpy(&data[1], payload, payload_length);
    g_reads++;
    return BSP_I2C2_OK;
}

static void reset_fake(void) {
    g_command = 0U;
    g_reads = 0U;
    g_bus_result = BSP_I2C2_OK;
    g_bad_count = false;
}

int main(void) {
    struct bq40z50_identity identity;

    reset_fake();
    assert(bq40z50_read_identity(&identity) == BQ40Z50_OK);
    assert(g_reads == 4U);
    assert(identity.device_type == 0x4550U);
    assert(identity.firmware.device_number == 0x4050U);
    assert(identity.firmware.version == 0x1234U);
    assert(identity.firmware.build == 0x5678U);
    assert(identity.firmware.firmware_type == 0x02U);
    assert(identity.firmware.impedance_track_version == 0xABCDU);
    assert(identity.firmware.execution_context == 0x01U);
    assert(identity.hardware_version == 0x1122U);
    assert(identity.chemistry_id == 0x3344U);

    reset_fake();
    g_bad_count = true;
    assert(bq40z50_read_identity(&identity) == BQ40Z50_ERR_PROTOCOL);

    reset_fake();
    g_bus_result = BSP_I2C2_ERR_TX_TIMEOUT;
    assert(bq40z50_read_identity(&identity) == BSP_I2C2_ERR_TX_TIMEOUT);
    assert(bq40z50_read_identity(NULL) == BQ40Z50_ERR_ARGUMENT);
    return 0;
}
